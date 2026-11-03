// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_threads.h>
#include <assert.h>
#include "../../../lib/libvlc_internal.h"
#include "../../../src/preparser/metadata_owner.c"
#include "../../../src/preparser/metadata_request_guard.c"
#include "../../../src/preparser/metadata_pipeline.c"

struct key_fixture;
struct request_key { vlc_atomic_rc_t refs; struct key_fixture *owner; unsigned magic; };
struct key_fixture {
    vlc_metadata_pipeline_t *pipeline;
    vlc_metadata_pipeline_stage_t *stage;
    struct request_key *key;
    vlc_sem_t cancel_entered, cancel_resume;
    atomic_uint ended, destroyed;
    bool close_on_release;
};
static void key_hold(void *opaque, const void *value)
{ (void)opaque; vlc_atomic_rc_inc(&((struct request_key *)value)->refs); }
static void key_release(void *opaque, const void *value)
{
    (void)opaque; struct request_key *key = (struct request_key *)value;
    if (!vlc_atomic_rc_dec(&key->refs)) return;
    struct key_fixture *fixture = key->owner;
    assert(key->magic == 123); key->magic = 0; atomic_fetch_add(&fixture->destroyed, 1);
    if (fixture->close_on_release) vlc_metadata_pipeline_Close(fixture->pipeline);
    free(key);
}
static int start_key(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{
    struct key_fixture *fixture = opaque; fixture->stage = stage;
    assert(vlc_metadata_pipeline_stage_GetKey(stage) == fixture->key);
    key_hold(NULL, fixture->key); return VLC_SUCCESS;
}
static void cancel_key(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{
    struct key_fixture *fixture = opaque;
    vlc_sem_post(&fixture->cancel_entered); vlc_sem_wait(&fixture->cancel_resume);
    assert(atomic_load(&fixture->destroyed) == 0);
    const struct request_key *key = vlc_metadata_pipeline_stage_GetKey(stage);
    assert(key->magic == 123);
}
static void key_ended(input_item_t *item, int status, struct vlc_metadata_local_events *events, void *opaque)
{
    (void)item; (void)events; struct key_fixture *fixture = opaque;
    assert(status == -EINTR); assert(atomic_fetch_add(&fixture->ended, 1) == 0);
    key_release(NULL, fixture->key); /* caller reference */
}
static void *cancel_thread(void *opaque)
{
    struct key_fixture *fixture = opaque;
    assert(vlc_metadata_pipeline_Cancel(fixture->pipeline, fixture->key) == 1); return NULL;
}
static void a_completed_provider_keeps_its_key_for_a_late_cancel_callback(bool reenter)
{
    struct key_fixture fixture = {.close_on_release=reenter};
    atomic_init(&fixture.ended, 0); atomic_init(&fixture.destroyed, 0);
    vlc_sem_init(&fixture.cancel_entered, 0); vlc_sem_init(&fixture.cancel_resume, 0);
    fixture.key = malloc(sizeof(*fixture.key)); assert(fixture.key != NULL);
    vlc_atomic_rc_init(&fixture.key->refs); fixture.key->owner = &fixture; fixture.key->magic = 123;
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    const struct vlc_metadata_pipeline_operations ops = {.local=start_key,.cancel_local=cancel_key,
        .retain_key=key_hold,.release_key=key_release};
    fixture.pipeline = vlc_metadata_pipeline_New(VLC_OBJECT(instance), &ops, &fixture); assert(fixture.pipeline != NULL);
    input_item_t *item = input_item_New("file:///private/key.flac", "Key"); assert(item != NULL);
    assert(vlc_metadata_pipeline_PushRequest(fixture.pipeline, item, VLC_PREPARSER_TYPE_PARSE,
        NULL, fixture.key, key_ended, &fixture) == VLC_SUCCESS);
    vlc_thread_t thread; assert(vlc_clone(&thread, cancel_thread, &fixture) == 0); vlc_sem_wait(&fixture.cancel_entered);
    vlc_metadata_pipeline_stage_Done(fixture.stage, -EINTR); fixture.stage = NULL;
    assert(atomic_load(&fixture.ended) == 1); key_release(NULL, fixture.key); /* provider reference */
    assert(atomic_load(&fixture.destroyed) == 0);
    vlc_sem_post(&fixture.cancel_resume); vlc_join(thread, NULL);
    assert(atomic_load(&fixture.destroyed) == 1);
    vlc_metadata_pipeline_Delete(fixture.pipeline); input_item_Release(item); libvlc_InternalDestroy(instance);
}
int main(void)
{
    a_completed_provider_keeps_its_key_for_a_late_cancel_callback(false);
    a_completed_provider_keeps_its_key_for_a_late_cancel_callback(true);
    return 0;
}
