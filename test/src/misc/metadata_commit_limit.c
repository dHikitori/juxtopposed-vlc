// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_threads.h>
#include <assert.h>

/* This executable owns its production sequence. No fault API enters core. */
int metadata_limit_unused_writer(vlc_object_t *, input_item_t *);
#define input_item_WriteMeta metadata_limit_unused_writer
#include "../../../src/input/meta.c"
#undef input_item_WriteMeta

static vlc_meta_t *competing_meta;
static bool saturate_between_writes;
static void consume_revision(vlc_meta_t *meta, vlc_meta_type_t type, char *value, uint64_t revision)
{
    if (saturate_between_writes) {
        saturate_between_writes = false;
        vlc_meta_Set(competing_meta, vlc_meta_Title, "Concurrent metadata");
        assert(vlc_meta_FieldRevision(competing_meta, vlc_meta_Title) == UINT64_MAX);
    }
    vlc_meta_SetOwnedRevision(meta, type, value, revision);
}
#define vlc_meta_SetOwnedRevision consume_revision
#include "../../../src/preparser/metadata_backend.c"
#undef vlc_meta_SetOwnedRevision

int metadata_limit_baseline_main(void);
#define main metadata_limit_baseline_main
#include "metadata_commit.c"
#undef main

static void failed_range_leaves_target_unchanged(void)
{
    struct fixture fixture; init(&fixture); start(&fixture); complete(&fixture);
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence);
    struct vlc_metadata_revisions before, after;
    vlc_mutex_lock(&fixture.item->lock); assert(revisions_locked(fixture.item, &before)); vlc_mutex_unlock(&fixture.item->lock);
    atomic_store(&metadata_revision_sequence, UINT64_MAX - 2);
    size_t applied = 99; assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == -EAGAIN && applied == 0);
    assert(atomic_load(&metadata_revision_sequence) == UINT64_MAX - 2);
    vlc_mutex_lock(&fixture.item->lock); assert(revisions_locked(fixture.item, &after)); vlc_mutex_unlock(&fixture.item->lock);
    assert(!memcmp(&before, &after, sizeof(before)));
    field(fixture.item, vlc_meta_Description, NULL); field(fixture.item, vlc_meta_Genre, NULL);
    clean(&fixture); atomic_store(&metadata_revision_sequence, saved);
}
static void preserved_fields_need_no_token(void)
{
    struct fixture fixture; init(&fixture); input_item_SetDescription(fixture.item, "Local description"); start(&fixture); complete(&fixture);
    vlc_mutex_lock(&fixture.item->lock);
    const uint64_t description = vlc_meta_FieldRevision(fixture.item->p_meta, vlc_meta_Description);
    vlc_mutex_unlock(&fixture.item->lock);
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence); atomic_store(&metadata_revision_sequence, UINT64_MAX - 2);
    size_t applied = 0; assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == VLC_SUCCESS && applied == 1);
    vlc_mutex_lock(&fixture.item->lock);
    assert(vlc_meta_FieldRevision(fixture.item->p_meta, vlc_meta_Description) == description);
    assert(vlc_meta_FieldRevision(fixture.item->p_meta, vlc_meta_Genre) == UINT64_MAX - 1);
    vlc_mutex_unlock(&fixture.item->lock);
    field(fixture.item, vlc_meta_Description, "Local description"); clean(&fixture); atomic_store(&metadata_revision_sequence, saved);
}
static void exhaustion_between_writes_keeps_reserved_tokens(void)
{
    struct fixture fixture; init(&fixture); start(&fixture); complete(&fixture);
    competing_meta = vlc_meta_New(); assert(competing_meta != NULL);
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence); atomic_store(&metadata_revision_sequence, UINT64_MAX - 3);
    saturate_between_writes = true; size_t applied = 0;
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == VLC_SUCCESS && applied == 2);
    assert(atomic_load(&metadata_revision_sequence) == UINT64_MAX);
    vlc_mutex_lock(&fixture.item->lock);
    assert(vlc_meta_FieldRevision(fixture.item->p_meta, vlc_meta_Genre) == UINT64_MAX - 2);
    assert(vlc_meta_FieldRevision(fixture.item->p_meta, vlc_meta_Description) == UINT64_MAX - 1);
    vlc_mutex_unlock(&fixture.item->lock);
    field(fixture.item, vlc_meta_Genre, "Provider genre"); field(fixture.item, vlc_meta_Description, "Provider description");
    vlc_meta_Delete(competing_meta); competing_meta = NULL; clean(&fixture); atomic_store(&metadata_revision_sequence, saved);
}
struct reservation { vlc_sem_t *start; uint64_t first; bool accepted; };
static void *reserve_last_token(void *opaque)
{
    struct reservation *request = opaque; vlc_sem_wait(request->start);
    request->accepted = vlc_meta_ReserveRevisions(1, &request->first); return NULL;
}
static void concurrent_reservations_do_not_overlap(void)
{
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence);
    for (unsigned round = 0; round < 32; ++round) {
        atomic_store(&metadata_revision_sequence, UINT64_MAX - 3);
        vlc_sem_t start; vlc_sem_init(&start, 0); struct reservation requests[8] = {0}; vlc_thread_t threads[8];
        for (size_t i = 0; i < ARRAY_SIZE(requests); ++i) {
            requests[i].start = &start; assert(vlc_clone(&threads[i], reserve_last_token, &requests[i]) == 0);
        }
        for (size_t i = 0; i < ARRAY_SIZE(requests); ++i) vlc_sem_post(&start);
        unsigned accepted = 0; uint64_t previous = 0;
        for (size_t i = 0; i < ARRAY_SIZE(requests); ++i) {
            vlc_join(threads[i], NULL); if (!requests[i].accepted) continue;
            assert(requests[i].first == UINT64_MAX - 2 || requests[i].first == UINT64_MAX - 1);
            assert(requests[i].first != previous); previous = requests[i].first; ++accepted;
        }
        assert(accepted == 2 && atomic_load(&metadata_revision_sequence) == UINT64_MAX - 1);
    }
    atomic_store(&metadata_revision_sequence, saved);
}
static void single_token_failure_retains_the_value(void)
{
    vlc_meta_t *meta = vlc_meta_New(); assert(meta != NULL); vlc_meta_Set(meta, vlc_meta_Title, "Local title");
    const uint64_t revision = vlc_meta_FieldRevision(meta, vlc_meta_Title);
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence); atomic_store(&metadata_revision_sequence, UINT64_MAX - 1);
    char *value = strdup("New title"); assert(value != NULL); assert(!vlc_meta_TrySetOwned(meta, vlc_meta_Title, value));
    assert(vlc_meta_FieldRevision(meta, vlc_meta_Title) == revision && !strcmp(vlc_meta_Get(meta, vlc_meta_Title), "Local title"));
    free(value); vlc_meta_Delete(meta); atomic_store(&metadata_revision_sequence, saved);
}
int main(void)
{
    failed_range_leaves_target_unchanged(); preserved_fields_need_no_token(); exhaustion_between_writes_keeps_reserved_tokens();
    concurrent_reservations_do_not_overlap(); single_token_failure_retains_the_value(); return 0;
}
