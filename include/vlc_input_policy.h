// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_INPUT_POLICY_H
#define VLC_INPUT_POLICY_H 1
#include <vlc_common.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct vlc_input_policy vlc_input_policy_t;
struct vlc_input_policy_operations {
    /** These callbacks can run on any input or network thread. They must use
     * only host-owned policy data. Do not call Qt or wait for a UI event.
     */
    bool (*allow_url)(void *opaque, const char *url);
    bool (*allow_connection)(void *opaque, const char *host, unsigned port, bool datagram);
    void (*destroy)(void *opaque);
};
VLC_API vlc_input_policy_t *vlc_input_policy_New(const struct vlc_input_policy_operations *, void *opaque);
/** Core-owned proxy scope. port=0 permits only the exact admitted source URI.
 * A nonzero port permits HTTP to literal 127.0.0.1 under /token/. The host
 * proxy must map opaque resource paths to approved remote requests.
 */
VLC_API vlc_input_policy_t *vlc_input_policy_NewProxy(unsigned port, const char *path_prefix,
                                                   const char *source_uri, bool attachments);
/** Inspect core-owned profiles. The caller must hold the policy. Source
 * profiles permit only the exact admitted URI and no connection. Proxy
 * profiles permit the requested loopback resource, without source or local
 * attachment access. A revoked profile is refused.
 */
VLC_API bool vlc_input_policy_IsSource(vlc_input_policy_t *, const char *source_uri);
VLC_API bool vlc_input_policy_IsProxy(vlc_input_policy_t *, const char *proxy_uri);
/** Atomically get or create the exact source-only item policy. Refuse an
 * unrelated existing policy or a changed item URI. Return a held reference.
 */
VLC_API vlc_input_policy_t *vlc_input_policy_HoldSourceItem(input_item_t *, const char *source_uri);
VLC_API vlc_input_policy_t *vlc_input_policy_Hold(vlc_input_policy_t *);
VLC_API void vlc_input_policy_Release(vlc_input_policy_t *);
VLC_API void vlc_input_policy_Revoke(vlc_input_policy_t *);
/** Return true after revocation. NULL has no revoked policy. */
VLC_API bool vlc_input_policy_IsRevoked(const vlc_input_policy_t *);
/** Attach a native policy to an item. Copies keep the reference. A conflicting
 * policy changes this item to deny all requests. Text options cannot set it.
 */
VLC_API int vlc_input_policy_AttachItem(input_item_t *, vlc_input_policy_t *);
VLC_API vlc_input_policy_t *vlc_input_policy_HoldItem(input_item_t *);
/** Child objects inherit a held policy. Attach intersects with an existing
 * policy; a different policy causes denial. Replace is a trusted native CAS
 * for the lazy source resolver. It never takes authority from URI text.
 */
VLC_API int vlc_input_policy_AttachObject(vlc_object_t *, vlc_input_policy_t *);
VLC_API int vlc_input_policy_ReplaceObject(vlc_object_t *, vlc_input_policy_t *expected, vlc_input_policy_t *replacement);
VLC_API vlc_input_policy_t *vlc_input_policy_HoldObject(vlc_object_t *);
VLC_API bool vlc_input_policy_Has(vlc_object_t *);
VLC_API bool vlc_input_policy_CheckURL(vlc_object_t *, const char *url);
VLC_API bool vlc_input_policy_CheckConnection(vlc_object_t *, const char *host, unsigned port, bool datagram);

#ifdef __cplusplus
}
#endif
#endif
