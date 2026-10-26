// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_threads.h>
#include <vlc_atomic.h>
#include <vlc_input_policy.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "policy.h"
#include "../misc/variables.h"

struct vlc_input_policy {
    vlc_atomic_rc_t refs;
    atomic_bool revoked;
    bool permanent;
    struct vlc_input_policy_operations ops;
    void *opaque;
};
static vlc_input_policy_t denied = { .revoked = ATOMIC_VAR_INIT(true), .permanent = true };

struct proxy_scope { char *prefix, *source; unsigned port; bool attachments; };
static bool path_character(unsigned char c)
{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
static bool proxy_url(void *opaque, const char *url)
{
    const struct proxy_scope *scope = opaque;
    if (scope->source != NULL && !strcmp(scope->source, url)) return true;
    if (scope->attachments && !strncmp(url, "attachment://", 13)) return true;
    if (scope->prefix == NULL || strncmp(scope->prefix, url, strlen(scope->prefix))) return false;
    const char *path = url + strlen(scope->prefix); unsigned segments = 1, length = 0;
    if (*path == '\0' || *path == '/') return false;
    for (; *path != '\0'; ++path) {
        if (++length > 512) return false;
        if (*path == '/') { if (++segments > 4 || path[1] == '\0' || path[1] == '/') return false; }
        else if (!path_character((unsigned char)*path)) return false;
    }
    return true;
}
static bool proxy_connection(void *opaque, const char *host, unsigned port, bool datagram)
{
    const struct proxy_scope *scope = opaque;
    return !datagram && scope->port != 0 && port == scope->port && !strcmp(host, "127.0.0.1");
}
static void proxy_destroy(void *opaque)
{ struct proxy_scope *scope = opaque; free(scope->prefix); free(scope->source); free(scope); }
static bool core_proxy(vlc_input_policy_t *policy)
{
    return policy != NULL && !atomic_load_explicit(&policy->revoked, memory_order_acquire)
        && policy->ops.allow_url == proxy_url && policy->ops.allow_connection == proxy_connection
        && policy->ops.destroy == proxy_destroy;
}
bool vlc_input_policy_IsSource(vlc_input_policy_t *policy, const char *source)
{
    if (!core_proxy(policy) || source == NULL || strnlen(source, 12001) > 12000
        || strncmp(source, "vlc-addon-source:v1:", 20)) return false;
    const struct proxy_scope *scope = policy->opaque;
    return scope->port == 0 && !scope->attachments && scope->source != NULL && !strcmp(scope->source, source);
}
bool vlc_input_policy_IsProxy(vlc_input_policy_t *policy, const char *uri)
{
    if (!core_proxy(policy) || uri == NULL || strnlen(uri, 1025) > 1024) return false;
    const struct proxy_scope *scope = policy->opaque;
    return scope->port != 0 && !scope->attachments && scope->source == NULL && proxy_url(policy->opaque, uri);
}
vlc_input_policy_t *vlc_input_policy_NewProxy(unsigned port, const char *path, const char *source, bool attachments)
{
    if (port > 65535 || (source != NULL && strnlen(source, 16385) > 16384)) return NULL;
    if (port != 0) {
        if (path == NULL) return NULL;
        const size_t length = strnlen(path, 131);
        if (length < 3 || length > 130 || path[0] != '/' || path[length - 1] != '/') return NULL;
        for (size_t i = 1; i < length - 1; ++i) if (!path_character((unsigned char)path[i])) return NULL;
    } else if (source == NULL || source[0] == '\0') return NULL;
    struct proxy_scope *scope = calloc(1, sizeof (*scope)); if (scope == NULL) return NULL;
    scope->port = port; scope->attachments = attachments;
    if (source != NULL && (scope->source = strdup(source)) == NULL) { proxy_destroy(scope); return NULL; }
    if (port != 0 && asprintf(&scope->prefix, "http://127.0.0.1:%u%s", port, path) < 0) { scope->prefix = NULL; proxy_destroy(scope); return NULL; }
    static const struct vlc_input_policy_operations ops = { proxy_url, proxy_connection, proxy_destroy };
    vlc_input_policy_t *policy = vlc_input_policy_New(&ops, scope);
    if (policy == NULL) proxy_destroy(scope);
    return policy;
}

vlc_input_policy_t *vlc_input_policy_New(const struct vlc_input_policy_operations *ops, void *opaque)
{
    if (ops == NULL || ops->allow_url == NULL || ops->allow_connection == NULL) return NULL;
    vlc_input_policy_t *policy = malloc(sizeof (*policy));
    if (policy == NULL) return NULL;
    vlc_atomic_rc_init(&policy->refs); atomic_init(&policy->revoked, false);
    policy->permanent = false; policy->ops = *ops; policy->opaque = opaque;
    return policy;
}
vlc_input_policy_t *vlc_input_policy_Hold(vlc_input_policy_t *policy)
{ if (policy != NULL && !policy->permanent) vlc_atomic_rc_inc(&policy->refs); return policy; }
void vlc_input_policy_Release(vlc_input_policy_t *policy)
{
    if (policy == NULL || policy->permanent || !vlc_atomic_rc_dec(&policy->refs)) return;
    if (policy->ops.destroy != NULL) policy->ops.destroy(policy->opaque);
    free(policy);
}
void vlc_input_policy_Revoke(vlc_input_policy_t *policy)
{ if (policy != NULL) atomic_store_explicit(&policy->revoked, true, memory_order_release); }
bool vlc_input_policy_IsRevoked(const vlc_input_policy_t *policy)
{ return policy != NULL && atomic_load_explicit(&policy->revoked, memory_order_acquire); }
vlc_input_policy_t *vlc_input_policy_HoldObject(vlc_object_t *obj)
{
    if (obj == NULL) return NULL;
    vlc_object_internals_t *priv = vlc_internals(obj);
    vlc_mutex_lock(&priv->var_lock); vlc_input_policy_t *policy = vlc_input_policy_Hold(priv->input_policy); vlc_mutex_unlock(&priv->var_lock);
    return policy;
}
int vlc_input_policy_AttachObject(vlc_object_t *obj, vlc_input_policy_t *policy)
{
    if (obj == NULL || policy == NULL) return VLC_EGENERIC;
    vlc_object_internals_t *priv = vlc_internals(obj);
    vlc_mutex_lock(&priv->var_lock); vlc_input_policy_t *old = priv->input_policy;
    const bool conflict = old != NULL && old != policy;
    priv->input_policy = vlc_input_policy_Hold(conflict ? &denied : policy);
    vlc_mutex_unlock(&priv->var_lock); vlc_input_policy_Release(old);
    return conflict ? VLC_EGENERIC : VLC_SUCCESS;
}
int vlc_input_policy_ReplaceObject(vlc_object_t *obj, vlc_input_policy_t *expected, vlc_input_policy_t *replacement)
{
    if (obj == NULL || expected == NULL || replacement == NULL) return VLC_EGENERIC;
    vlc_object_internals_t *priv = vlc_internals(obj);
    vlc_mutex_lock(&priv->var_lock);
    if (priv->input_policy != expected) { vlc_mutex_unlock(&priv->var_lock); return VLC_EGENERIC; }
    priv->input_policy = vlc_input_policy_Hold(replacement);
    vlc_mutex_unlock(&priv->var_lock); vlc_input_policy_Release(expected); return VLC_SUCCESS;
}
bool vlc_input_policy_Has(vlc_object_t *obj)
{
    vlc_input_policy_t *policy = vlc_input_policy_HoldObject(obj); const bool found = policy != NULL;
    vlc_input_policy_Release(policy); return found;
}
bool vlc_input_policy_CheckURL(vlc_object_t *obj, const char *url)
{
    vlc_input_policy_t *policy = vlc_input_policy_HoldObject(obj);
    if (policy == NULL) return true;
    const bool allowed = !atomic_load_explicit(&policy->revoked, memory_order_acquire) && url != NULL
        && strnlen(url, 16385) <= 16384 && policy->ops.allow_url(policy->opaque, url);
    vlc_input_policy_Release(policy); return allowed;
}
bool vlc_input_policy_CheckConnection(vlc_object_t *obj, const char *host, unsigned port, bool datagram)
{
    vlc_input_policy_t *policy = vlc_input_policy_HoldObject(obj);
    if (policy == NULL) return true;
    const bool allowed = !atomic_load_explicit(&policy->revoked, memory_order_acquire) && host != NULL
        && strnlen(host, 254) <= 253 && port > 0 && port <= 65535 && policy->ops.allow_connection(policy->opaque, host, port, datagram);
    vlc_input_policy_Release(policy); return allowed;
}

// Item helpers use this policy when two independent scopes conflict.
vlc_input_policy_t *vlc_input_policy_Denied(void) { return &denied; }
