// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <vlc_common.h>
#include <vlc_network.h>
#include <vlc_tls.h>
#include <vlc_objects.h>
#include <vlc_input_policy.h>

static struct vlc_logger logger;
static vlc_object_t *owner;
static vlc_tls_client_t credentials;
static unsigned lookups, opens, creates, destroys;

static char *test_proxy(vlc_object_t *obj, const char *url)
{
    assert(obj == owner);
    assert(!strcmp(url, "https://media.invalid/test.wav"));
    lookups++;
    return strdup("http://user:password@proxy.invalid:8123/");
}

static vlc_tls_client_t *test_credentials_create(vlc_object_t *obj)
{
    assert(obj == owner);
    creates++;
    return &credentials;
}

static void test_credentials_delete(vlc_tls_client_t *creds)
{
    assert(creds == &credentials);
    destroys++;
}

static vlc_tls_t *test_connect_proxy(void *ctx, vlc_tls_client_t *creds,
    const char *host, unsigned port, bool *restrict two, const char *proxy)
{
    /* A credentials object is not a logger. Using it here crashes when the
     * CONNECT request logs through its connection context. */
    assert(ctx == &logger);
    assert(creds == &credentials);
    assert(!strcmp(host, "media.invalid") && port == 443 && *two);
    assert(!strcmp(proxy, "http://user:password@proxy.invalid:8123/"));
    opens++;
    return NULL;
}

/* Compile the real manager with only credential loading, proxy discovery and
 * the transport boundary replaced. Message building and manager state are real. */
#define vlc_getProxyUrlForObject test_proxy
#define vlc_tls_ClientCreate test_credentials_create
#define vlc_tls_ClientDelete test_credentials_delete
#define vlc_https_connect_proxy test_connect_proxy
#include "connmgr.c"
#undef vlc_https_connect_proxy
#undef vlc_tls_ClientDelete
#undef vlc_tls_ClientCreate
#undef vlc_getProxyUrlForObject

int main(void)
{
    owner = vlc_object_create((vlc_object_t *)NULL, sizeof (*owner));
    assert(owner != NULL);
    owner->logger = &logger;
    struct vlc_http_mgr *mgr = vlc_http_mgr_create(owner, NULL);
    assert(mgr);
    struct vlc_http_msg *request = vlc_http_req_create("GET", "https",
        "media.invalid", "/test.wav");
    assert(request);
    for (unsigned i = 0; i < 2; ++i)
        assert(vlc_http_mgr_request(mgr, true, "media.invalid", 443, request,
                                    true, false) == NULL);
    assert(lookups == 2 && opens == 2 && creates == 1);
    vlc_input_policy_t *policy = vlc_input_policy_NewProxy(12345,
        "/admitted/", "hakone-addon-source://example/item", false);
    assert(policy != NULL);
    assert(vlc_input_policy_AttachObject(owner, policy) == VLC_SUCCESS);
    vlc_input_policy_Release(policy);
    assert(vlc_http_mgr_request(mgr, true, "media.invalid", 443, request,
                                true, false) == NULL);
    assert(lookups == 2 && opens == 2 && creates == 1);
    vlc_http_msg_destroy(request);
    vlc_http_mgr_destroy(mgr);
    assert(destroys == 1);
    vlc_object_delete(owner);
    return 0;
}
