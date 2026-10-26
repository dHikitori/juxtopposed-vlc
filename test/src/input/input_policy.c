// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_policy.h>
#include <vlc_input_item.h>
#include <vlc_tls.h>
#include <vlc_network.h>
#include <vlc_threads.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct state { unsigned destroyed; vlc_sem_t entered, resume; bool wait; };
static bool allow_url(void *opaque, const char *url)
{
    struct state *state = opaque;
    if (state->wait) { vlc_sem_post(&state->entered); vlc_sem_wait(&state->resume); }
    return !strcmp(url, "https://approved.example.test/item");
}
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; return !datagram && port == 9876 && !strcmp(host, "127.0.0.1"); }
static void destroy(void *opaque) { ++((struct state *)opaque)->destroyed; }
static vlc_input_policy_t *policy_new(struct state *state)
{
    static const struct vlc_input_policy_operations ops = { allow_url, allow_connection, destroy };
    vlc_input_policy_t *policy = vlc_input_policy_New(&ops, state); assert(policy != NULL); return policy;
}
static vlc_object_t *object_new(vlc_object_t *parent)
{ vlc_object_t *obj = vlc_object_create(parent, sizeof (*obj)); assert(obj != NULL); return obj; }

static void inheritance_and_revocation(void)
{
    struct state state = {0}; vlc_object_t *root = object_new(NULL);
    assert(!vlc_input_policy_Has(root)); assert(vlc_input_policy_CheckURL(root, "file:///normal"));
    vlc_input_policy_t *policy = policy_new(&state);
    assert(vlc_input_policy_AttachObject(root, policy) == VLC_SUCCESS);
    vlc_object_t *child = object_new(root); vlc_input_policy_Release(policy);
    assert(vlc_input_policy_Has(child));
    assert(vlc_input_policy_CheckURL(child, "https://approved.example.test/item"));
    assert(!vlc_input_policy_CheckURL(child, "https://approved.example.test.attacker.invalid/item"));
    assert(vlc_input_policy_CheckConnection(child, "127.0.0.1", 9876, false));
    assert(!vlc_input_policy_CheckConnection(child, "127.0.0.1", 9876, true));
    assert(!vlc_input_policy_CheckConnection(child, "localhost", 9876, false));
    policy = vlc_input_policy_HoldObject(child); vlc_input_policy_Revoke(policy); vlc_input_policy_Release(policy);
    assert(vlc_input_policy_Has(child)); assert(!vlc_input_policy_CheckURL(child, "https://approved.example.test/item"));
    assert(!vlc_input_policy_CheckConnection(child, "127.0.0.1", 9876, false));
    vlc_object_delete(child); assert(state.destroyed == 0); vlc_object_delete(root); assert(state.destroyed == 1);
}
static void copied_items_keep_policy(void)
{
    struct state state = {0}; vlc_input_policy_t *policy = policy_new(&state);
    input_item_t *item = input_item_New("https://approved.example.test/item", "Policy test"); assert(item != NULL);
    assert(vlc_input_policy_AttachItem(item, policy) == VLC_SUCCESS); vlc_input_policy_Release(policy);
    input_item_t *copy = input_item_Copy(item); assert(copy != NULL); input_item_Release(item);
    policy = vlc_input_policy_HoldItem(copy); assert(policy != NULL);
    vlc_object_t *obj = object_new(NULL); assert(vlc_input_policy_AttachObject(obj, policy) == VLC_SUCCESS);
    vlc_input_policy_Release(policy); input_item_Release(copy); assert(state.destroyed == 0);
    assert(!vlc_input_policy_CheckURL(obj, "file:///private")); vlc_object_delete(obj); assert(state.destroyed == 1);
}
static void conflicts_and_native_cas(void)
{
    struct state first = {0}, second = {0}; vlc_input_policy_t *a = policy_new(&first), *b = policy_new(&second);
    vlc_object_t *obj = object_new(NULL); assert(vlc_input_policy_AttachObject(obj, a) == VLC_SUCCESS);
    assert(vlc_input_policy_ReplaceObject(obj, b, a) != VLC_SUCCESS);
    assert(vlc_input_policy_ReplaceObject(obj, a, b) == VLC_SUCCESS);
    assert(vlc_input_policy_AttachObject(obj, a) != VLC_SUCCESS);
    assert(!vlc_input_policy_CheckURL(obj, "https://approved.example.test/item"));
    input_item_t *item = input_item_New("vlc://nop", "Conflict"); assert(item != NULL);
    assert(vlc_input_policy_AttachItem(item, a) == VLC_SUCCESS);
    assert(vlc_input_policy_AttachItem(item, b) != VLC_SUCCESS);
    vlc_input_policy_t *denied = vlc_input_policy_HoldItem(item); assert(denied != NULL);
    vlc_object_t *child = object_new(NULL); assert(vlc_input_policy_AttachObject(child, denied) == VLC_SUCCESS);
    assert(!vlc_input_policy_CheckConnection(child, "127.0.0.1", 9876, false));
    vlc_input_policy_Release(denied); input_item_Release(item); vlc_object_delete(child); vlc_object_delete(obj);
    vlc_input_policy_Release(a); vlc_input_policy_Release(b); assert(first.destroyed == 1 && second.destroyed == 1);
}
static void text_options_do_not_set_policy(void)
{
    input_item_t *item = input_item_New("vlc://nop", "Text option"); assert(item != NULL);
    assert(input_item_AddOption(item, "input-policy=allow-all", VLC_INPUT_OPTION_TRUSTED) == VLC_SUCCESS);
    assert(vlc_input_policy_HoldItem(item) == NULL);
    vlc_object_t *obj = object_new(NULL); var_Create(obj, "input-policy", VLC_VAR_STRING); var_SetString(obj, "input-policy", "allow-all");
    assert(!vlc_input_policy_Has(obj)); vlc_object_delete(obj); input_item_Release(item);
}
static void proxy_profile(void)
{
    vlc_input_policy_t *policy = vlc_input_policy_NewProxy(9876, "/opaque_grant/", "vlc-addon-source:v1:admitted", true); assert(policy != NULL);
    vlc_object_t *obj = object_new(NULL); assert(vlc_input_policy_AttachObject(obj, policy) == VLC_SUCCESS); vlc_input_policy_Release(policy);
    for (const char *const *p = (const char *const[]){"http://127.0.0.1:9876/opaque_grant/resource", "http://127.0.0.1:9876/opaque_grant/template/20", "attachment://cover.jpg", "vlc-addon-source:v1:admitted", NULL}; *p; ++p)
        assert(vlc_input_policy_CheckURL(obj, *p));
    for (const char *const *p = (const char *const[]){"http://127.0.0.1:9876/other/resource", "http://localhost:9876/opaque_grant/resource", "http://127.0.0.1:9877/opaque_grant/resource", "https://127.0.0.1:9876/opaque_grant/resource", "http://127.0.0.1:9876/opaque_grant/../private", "http://127.0.0.1:9876/opaque_grant/%2e%2e", "http://127.0.0.1:9876/opaque_grant//resource", "http://127.0.0.1:9876/opaque_grant/resource?url=private", "file:///private", "vlc-addon-source:v1:forged", NULL}; *p; ++p)
        assert(!vlc_input_policy_CheckURL(obj, *p));
    assert(vlc_input_policy_CheckConnection(obj, "127.0.0.1", 9876, false));
    assert(!vlc_input_policy_CheckConnection(obj, "127.0.0.1", 9877, false));
    assert(!vlc_input_policy_CheckConnection(obj, "127.0.0.1", 9876, true));
    assert(vlc_tls_SocketOpenTCP(obj, "127.0.0.1", 9877) == NULL);
    assert((net_Connect)(obj, "127.0.0.1", 9877, SOCK_STREAM, IPPROTO_TCP) == -1);
    vlc_object_delete(obj);
    assert(vlc_input_policy_NewProxy(9876, "/../", NULL, false) == NULL);
    assert(vlc_input_policy_NewProxy(0, NULL, NULL, false) == NULL);
    policy = vlc_input_policy_NewProxy(0, NULL, "vlc-addon-source:v1:admitted", false); assert(policy != NULL);
    obj = object_new(NULL); assert(vlc_input_policy_AttachObject(obj, policy) == VLC_SUCCESS); vlc_input_policy_Release(policy);
    assert(vlc_input_policy_CheckURL(obj, "vlc-addon-source:v1:admitted")); assert(!vlc_input_policy_CheckConnection(obj, "127.0.0.1", 9876, false));
    vlc_object_delete(obj);
}
static void *check_url_thread(void *opaque)
{ assert(vlc_input_policy_CheckURL(opaque, "https://approved.example.test/item")); return NULL; }
static void callbacks_keep_their_state(void)
{
    struct state first = { .wait = true }, second = {0}; vlc_sem_init(&first.entered, 0); vlc_sem_init(&first.resume, 0);
    vlc_input_policy_t *a = policy_new(&first), *b = policy_new(&second); vlc_object_t *obj = object_new(NULL);
    assert(vlc_input_policy_AttachObject(obj, a) == VLC_SUCCESS); vlc_thread_t thread; assert(vlc_clone(&thread, check_url_thread, obj) == 0);
    vlc_sem_wait(&first.entered); assert(vlc_input_policy_ReplaceObject(obj, a, b) == VLC_SUCCESS); vlc_input_policy_Release(a);
    assert(first.destroyed == 0); vlc_sem_post(&first.resume); vlc_join(thread, NULL); assert(first.destroyed == 1);
    vlc_input_policy_Release(b); vlc_object_delete(obj); assert(second.destroyed == 1);
}
int main(void)
{
    inheritance_and_revocation(); copied_items_keep_policy(); conflicts_and_native_cas();
    text_options_do_not_set_policy(); proxy_profile(); callbacks_keep_their_state(); return 0;
}
