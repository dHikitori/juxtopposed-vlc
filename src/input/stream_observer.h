// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_STREAM_OBSERVER_INTERNAL_H
#define VLC_STREAM_OBSERVER_INTERNAL_H 1

#include <vlc_common.h>

/* Native metadata evidence only. This object has no VM pointer or callback.
 * Attach before module activation. Every child object holds the same scope.
 * Stream reads use a retained private reference, including worker reads.
 */
typedef struct vlc_stream_observer vlc_stream_observer_t;

VLC_API vlc_stream_observer_t *vlc_stream_observer_New(void);
VLC_API void vlc_stream_observer_Release(vlc_stream_observer_t *);
VLC_API int vlc_stream_observer_AttachObject(vlc_object_t *, vlc_stream_observer_t *);

/* Call after VM close and after its stream workers have joined. Seal closes
 * admission even on failure. A negative needs clean EOF for every used stream,
 * no read error, and no live stream or read. Not-applicable also needs no I/O.
 * A failed filter candidate that never reads is neutral. A used stream closed
 * before EOF permanently refuses evidence. Close never waits for a worker.
 */
VLC_API bool vlc_stream_observer_Seal(vlc_stream_observer_t *, bool require_no_io);
VLC_API void vlc_stream_observer_Close(vlc_stream_observer_t *);
/* A transport may hide an error in its ordinary EOF return. Mark that fact
 * without changing its read result. Objects outside metadata have no scope. */
VLC_API void vlc_stream_observer_FailObject(vlc_object_t *);

/* Core internals. No observer lock spans an I/O or module callback. The caller
 * keeps the object alive while taking its held reference. Object association
 * locks precede the observer lock; the observer never locks an object.
 */
vlc_stream_observer_t *vlc_stream_observer_Hold(vlc_stream_observer_t *);
vlc_stream_observer_t *vlc_stream_observer_HoldObject(vlc_object_t *);
void vlc_stream_observer_Fail(vlc_stream_observer_t *);

struct vlc_stream_observer_stream {
    vlc_stream_observer_t *observer;
    bool used;
    bool complete;
};
int vlc_stream_observer_StreamInit(struct vlc_stream_observer_stream *, vlc_object_t *);
void vlc_stream_observer_StreamDestroy(struct vlc_stream_observer_stream *);
bool vlc_stream_observer_ReadBegin(struct vlc_stream_observer_stream *);
void vlc_stream_observer_ReadEnd(struct vlc_stream_observer_stream *,
                                bool error, bool eof);
void vlc_stream_observer_StreamFailure(struct vlc_stream_observer_stream *);
#endif
