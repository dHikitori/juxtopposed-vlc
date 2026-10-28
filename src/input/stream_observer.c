// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_threads.h>
#include <vlc_atomic.h>
#include <assert.h>
#include <stdlib.h>
#include "stream_observer.h"
#include "../misc/variables.h"

enum { STREAM_OBSERVER_LIMIT = 64 };
struct vlc_stream_observer {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    unsigned streams;
    unsigned reads;
    bool failed;
    bool closed;
    bool io;
};

vlc_stream_observer_t *vlc_stream_observer_New(void)
{
    vlc_stream_observer_t *observer = calloc(1, sizeof(*observer));
    if (observer == NULL) return NULL;
    vlc_atomic_rc_init(&observer->refs);
    vlc_mutex_init(&observer->lock);
    return observer;
}

vlc_stream_observer_t *vlc_stream_observer_Hold(vlc_stream_observer_t *observer)
{
    if (observer != NULL) vlc_atomic_rc_inc(&observer->refs);
    return observer;
}

void vlc_stream_observer_Release(vlc_stream_observer_t *observer)
{
    if (observer == NULL || !vlc_atomic_rc_dec(&observer->refs)) return;
    assert(observer->streams == 0 && observer->reads == 0);
    free(observer);
}

vlc_stream_observer_t *vlc_stream_observer_HoldObject(vlc_object_t *object)
{
    if (object == NULL) return NULL;
    vlc_object_internals_t *private = vlc_internals(object);
    vlc_mutex_lock(&private->var_lock);
    vlc_stream_observer_t *observer = vlc_stream_observer_Hold(private->stream_observer);
    vlc_mutex_unlock(&private->var_lock);
    return observer;
}

int vlc_stream_observer_AttachObject(vlc_object_t *object, vlc_stream_observer_t *observer)
{
    if (object == NULL || observer == NULL) return VLC_EGENERIC;
    vlc_object_internals_t *private = vlc_internals(object);
    vlc_mutex_lock(&private->var_lock);
    vlc_stream_observer_t *old = private->stream_observer;
    if (old == observer) {
        vlc_mutex_unlock(&private->var_lock);
        return VLC_SUCCESS;
    }
    bool conflict = false;
    if (old != NULL) {
        vlc_mutex_lock(&old->lock);
        conflict = !old->closed;
        vlc_mutex_unlock(&old->lock);
    }
    if (!conflict) private->stream_observer = vlc_stream_observer_Hold(observer);
    vlc_mutex_unlock(&private->var_lock);
    if (!conflict) vlc_stream_observer_Release(old);
    return conflict ? VLC_EGENERIC : VLC_SUCCESS;
}

bool vlc_stream_observer_Seal(vlc_stream_observer_t *observer, bool require_no_io)
{
    if (observer == NULL) return false;
    vlc_mutex_lock(&observer->lock);
    const bool valid = !observer->closed && !observer->failed
        && observer->streams == 0 && observer->reads == 0
        && (!require_no_io || !observer->io);
    observer->closed = true;
    vlc_mutex_unlock(&observer->lock);
    return valid;
}

void vlc_stream_observer_Close(vlc_stream_observer_t *observer)
{
    if (observer == NULL) return;
    vlc_mutex_lock(&observer->lock);
    observer->closed = true;
    vlc_mutex_unlock(&observer->lock);
}

void vlc_stream_observer_FailObject(vlc_object_t *object)
{
    vlc_stream_observer_t *observer = vlc_stream_observer_HoldObject(object);
    vlc_stream_observer_Fail(observer);
    vlc_stream_observer_Release(observer);
}

void vlc_stream_observer_Fail(vlc_stream_observer_t *observer)
{
    if (observer == NULL) return;
    vlc_mutex_lock(&observer->lock);
    observer->failed = true;
    vlc_mutex_unlock(&observer->lock);
}

int vlc_stream_observer_StreamInit(struct vlc_stream_observer_stream *stream,
                                  vlc_object_t *object)
{
    *stream = (struct vlc_stream_observer_stream){0};
    vlc_stream_observer_t *observer = vlc_stream_observer_HoldObject(object);
    if (observer == NULL) return VLC_SUCCESS;
    vlc_mutex_lock(&observer->lock);
    const bool accepted = !observer->closed && observer->streams < STREAM_OBSERVER_LIMIT;
    if (accepted) ++observer->streams;
    else observer->failed = true;
    vlc_mutex_unlock(&observer->lock);
    if (!accepted) {
        vlc_stream_observer_Release(observer);
        return VLC_EGENERIC;
    }
    stream->observer = observer;
    return VLC_SUCCESS;
}

void vlc_stream_observer_StreamDestroy(struct vlc_stream_observer_stream *stream)
{
    vlc_stream_observer_t *observer = stream->observer;
    if (observer == NULL) return;
    vlc_mutex_lock(&observer->lock);
    assert(observer->streams > 0);
    if (stream->used && !stream->complete) observer->failed = true;
    --observer->streams;
    vlc_mutex_unlock(&observer->lock);
    stream->observer = NULL;
    vlc_stream_observer_Release(observer);
}

bool vlc_stream_observer_ReadBegin(struct vlc_stream_observer_stream *stream)
{
    vlc_stream_observer_t *observer = stream->observer;
    if (observer == NULL) return true;
    vlc_mutex_lock(&observer->lock);
    const bool accepted = !observer->closed && observer->reads < STREAM_OBSERVER_LIMIT;
    observer->io = true;
    if (accepted) {
        ++observer->reads;
        stream->used = true;
        stream->complete = false;
    } else observer->failed = true;
    vlc_mutex_unlock(&observer->lock);
    return accepted;
}

void vlc_stream_observer_ReadEnd(struct vlc_stream_observer_stream *stream,
                                bool error, bool eof)
{
    vlc_stream_observer_t *observer = stream->observer;
    if (observer == NULL) return;
    vlc_mutex_lock(&observer->lock);
    assert(observer->reads > 0);
    --observer->reads;
    if (error) observer->failed = true;
    stream->complete = eof && !error;
    vlc_mutex_unlock(&observer->lock);
}

void vlc_stream_observer_StreamFailure(struct vlc_stream_observer_stream *stream)
{
    vlc_stream_observer_t *observer = stream->observer;
    if (observer == NULL) return;
    vlc_mutex_lock(&observer->lock);
    observer->failed = true;
    vlc_mutex_unlock(&observer->lock);
}
