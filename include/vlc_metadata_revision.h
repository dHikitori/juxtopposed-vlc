// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_REVISION_H
#define VLC_METADATA_REVISION_H 1
#include <vlc_common.h>
#include <vlc_meta.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Read a field token while the metadata object's normal lock is held.
 * Every accepted write has a new token, including a same-value write.
 * UINT64_MAX is an exhausted token. Do not commit a patch with that token.
 */
VLC_API uint64_t vlc_meta_FieldRevision(const vlc_meta_t *, vlc_meta_type_t);
VLC_API uint64_t vlc_meta_ExtraRevision(const vlc_meta_t *);

struct vlc_metadata_revisions {
    uint64_t fields[VLC_META_TYPE_COUNT];
    uint64_t extra;
};

/** Return a native snapshot and its source field tokens. The caller releases
 * the snapshot with input_item_Release(). A bounded retry rejects a source
 * that changes during the copy. No network or event loop is necessary.
 */
VLC_API input_item_t *vlc_metadata_item_Snapshot(input_item_t *, struct vlc_metadata_revisions *);

#ifdef __cplusplus
}
#endif
#endif
