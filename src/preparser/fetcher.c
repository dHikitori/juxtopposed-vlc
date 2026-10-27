/*****************************************************************************
 * fetcher.c
 *****************************************************************************
 * Copyright © 2017-2017 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_atomic.h>
#include <vlc_stream.h>
#include <vlc_modules.h>
#include <vlc_interrupt.h>
#include <vlc_arrays.h>
#include <vlc_threads.h>
#include <vlc_memstream.h>
#include <vlc_meta_fetcher.h>
#include <vlc_executor.h>

#include "art.h"
#include "../libvlc.h"
#include "fetcher.h"
#include "input/input_interface.h"
#include "misc/interrupt.h"
#include "metadata_owner.h"

#define MAX_TASKS 1024u
enum fetch_mode { FETCH_LEGACY, FETCH_OWNED_LOCAL, FETCH_NAMED_PROVIDER };

struct input_fetcher_t {
    vlc_atomic_rc_t refs;
    vlc_executor_t *executor_local;
    vlc_executor_t *executor_network;
    vlc_executor_t *executor_downloader;

    vlc_dictionary_t album_cache;
    vlc_object_t* owner;

    vlc_mutex_t lock;
    struct vlc_list submitted_tasks; /**< list of struct task */
    unsigned retained;
    bool closing, deleting;
};

struct task {
    vlc_atomic_rc_t refs;
    input_fetcher_t *fetcher;
    vlc_executor_t *executor;
    input_item_t* item;
    int options;
    const input_fetcher_callbacks_t *cbs;
    void *userdata;
    const void *key;
    enum fetch_mode mode;
    meta_fetcher_provider_t provider;
    vlc_metadata_pipeline_stage_t *local;
    vlc_metadata_job_t *job;
    input_fetcher_probe_cb probe;
    vlc_metadata_legacy_t *legacy;
    bool legacy_running, interrupt_attached, listed, retained;
    atomic_bool cancelled;

    vlc_interrupt_t interrupt;
    vlc_interrupt_t *previous_interrupt;

    struct vlc_runnable runnable; /**< to be passed to the executor */
    struct vlc_list node; /**< node of input_fetcher_t.submitted_tasks */
};

static void RunDownloader(void *);
static void RunSearchLocal(void *);
static void RunSearchNetwork(void *);

input_fetcher_t *input_fetcher_Hold(input_fetcher_t *fetcher)
{ vlc_atomic_rc_inc(&fetcher->refs); return fetcher; }
void input_fetcher_Release(input_fetcher_t *fetcher)
{
    if (!vlc_atomic_rc_dec(&fetcher->refs)) return;
    assert(fetcher->retained == 0 && vlc_list_is_empty(&fetcher->submitted_tasks));
    free(fetcher);
}
static struct task *TaskHold(struct task *task)
{ vlc_atomic_rc_inc(&task->refs); return task; }
static bool TaskCurrent(struct task *task)
{
    return !atomic_load(&task->cancelled)
        && !atomic_load(&task->interrupt.killed)
        && (task->legacy == NULL || !atomic_load(&vlc_metadata_legacy_Interrupt(task->legacy)->killed))
        && (task->local == NULL || vlc_metadata_pipeline_stage_IsCurrent(task->local))
        && (task->job == NULL || vlc_metadata_job_IsPending(task->job));
}

static struct task *
TaskNew(input_fetcher_t *fetcher, vlc_executor_t *executor, input_item_t *item,
        int options, const input_fetcher_callbacks_t *cbs, void *userdata,
        const void *key, enum fetch_mode mode, meta_fetcher_provider_t provider,
        vlc_metadata_pipeline_stage_t *local, vlc_metadata_job_t *job,
        input_fetcher_probe_cb probe, int *status)
{
    struct task *task = calloc(1, sizeof(*task));
    if (!task) { *status = VLC_ENOMEM; return NULL; }
    if (mode == FETCH_LEGACY) {
        task->legacy = vlc_metadata_legacy_Acquire(fetcher->owner);
        if (task->legacy == NULL) { free(task); *status = -EINTR; return NULL; }
    }

    vlc_atomic_rc_init(&task->refs);
    atomic_init(&task->cancelled, false);
    task->fetcher = input_fetcher_Hold(fetcher);
    task->executor = executor;
    task->item = item;
    task->options = options;
    task->cbs = cbs;
    task->userdata = userdata;
    task->key = key; task->mode = mode; task->provider = provider;
    task->local = local != NULL ? vlc_metadata_pipeline_stage_Hold(local) : NULL;
    task->job = job != NULL ? vlc_metadata_job_Hold(job) : NULL;
    task->probe = probe;

    vlc_interrupt_init(&task->interrupt);

    input_item_Hold(item);

    if (executor == fetcher->executor_local)
        task->runnable.run = RunSearchLocal;
    else if (executor == fetcher->executor_network)
        task->runnable.run = RunSearchNetwork;
    else
        task->runnable.run = RunDownloader;

    task->runnable.userdata = task;

    return task;
}

static void
TaskDelete(struct task *task)
{
    if (!vlc_atomic_rc_dec(&task->refs)) return;
    input_fetcher_t *fetcher = task->fetcher;
    assert(!task->listed && !task->legacy_running);
    input_item_Release(task->item);
    if (task->local != NULL) vlc_metadata_pipeline_stage_Release(task->local);
    if (task->job != NULL) vlc_metadata_job_Release(task->job);
    vlc_metadata_legacy_Release(task->legacy);
    vlc_interrupt_deinit(&task->interrupt);
    vlc_mutex_lock(&fetcher->lock);
    if (task->retained) --fetcher->retained;
    vlc_mutex_unlock(&fetcher->lock);
    free(task);
    input_fetcher_Release(fetcher);
}

static void
FetcherRemoveTask(input_fetcher_t *fetcher, struct task *task)
{
    vlc_mutex_lock(&fetcher->lock);
    if (task->listed) { vlc_list_remove(&task->node); task->listed = false; }
    vlc_mutex_unlock(&fetcher->lock);
}

static int
Submit(input_fetcher_t *fetcher, vlc_executor_t *executor, input_item_t *item,
       int options, const input_fetcher_callbacks_t *cbs, void *userdata,
       const void *key, enum fetch_mode mode, meta_fetcher_provider_t provider,
       vlc_metadata_pipeline_stage_t *local, vlc_metadata_job_t *job,
       input_fetcher_probe_cb probe, struct task *parent)
{
    int status;
    struct task *task =
        TaskNew(fetcher, executor, item, options, cbs, userdata, key, mode, provider, local, job, probe, &status);
    if (!task)
        return status;

    vlc_mutex_lock(&fetcher->lock);
    if (fetcher->closing || fetcher->deleting || !TaskCurrent(task) || (parent != NULL && !TaskCurrent(parent))
        || fetcher->retained >= MAX_TASKS)
    {
        status = fetcher->retained >= MAX_TASKS ? VLC_ENOMEM : -EINTR;
        vlc_mutex_unlock(&fetcher->lock);
        TaskDelete(task);
        return status;
    }
    /* Publish and enqueue together: cancellation must never see a task whose
     * runnable has not yet been submitted to its executor. */
    vlc_list_append(&task->node, &fetcher->submitted_tasks);
    task->listed = task->retained = true; ++fetcher->retained;
    vlc_executor_Submit(task->executor, &task->runnable);
    vlc_mutex_unlock(&fetcher->lock);

    return VLC_SUCCESS;
}

static char* CreateCacheKey( input_item_t* item )
{
    vlc_mutex_lock( &item->lock );

    if( !item->p_meta )
    {
        vlc_mutex_unlock( &item->lock );
        return NULL;
    }

    char const* artist = vlc_meta_Get( item->p_meta, vlc_meta_Artist );
    char const* album = vlc_meta_Get( item->p_meta, vlc_meta_Album );
    char const *date = vlc_meta_Get( item->p_meta, vlc_meta_Date );
    char* key;

    /**
     * Simple concatenation of artist and album can lead to the same key
     * for entities that should not have such. Imagine { dogs, tick } and
     * { dog, stick } */
    if( !artist || !album || asprintf( &key, "%s:%zu:%s:%zu:%s",
          artist, strlen( artist ), album, strlen( album ),
          date ? date : "0000" ) < 0 )
    {
        key = NULL;
    }
    vlc_mutex_unlock( &item->lock );

    return key;
}

static void FreeCacheEntry( void* data, void* obj )
{
    free( data );
    VLC_UNUSED( obj );
}

static int ReadAlbumCache( input_fetcher_t* fetcher, input_item_t* item )
{
    if (vlc_artwork_item_IsRestricted(item)) return VLC_EGENERIC;
    char* key = CreateCacheKey( item );

    if( key == NULL )
        return VLC_EGENERIC;

    vlc_mutex_lock( &fetcher->lock );
    char const* art = vlc_dictionary_value_for_key( &fetcher->album_cache,
                                                    key );
    if( art )
        input_item_SetArtURL( item, art );
    vlc_mutex_unlock( &fetcher->lock );

    free( key );
    return art ? VLC_SUCCESS : VLC_EGENERIC;
}

static void AddAlbumCache( input_fetcher_t* fetcher, input_item_t* item,
                           bool overwrite )
{
    if (vlc_artwork_item_IsRestricted(item)) return;
    char* art = input_item_GetArtURL( item );
    char* key = CreateCacheKey( item );

    if( key && art && strncasecmp( art, "attachment://", 13 ) )
    {
        vlc_mutex_lock( &fetcher->lock );
        if( overwrite || !vlc_dictionary_has_key( &fetcher->album_cache, key ) )
        {
            vlc_dictionary_insert( &fetcher->album_cache, key, art );
            art = NULL;
        }
        vlc_mutex_unlock( &fetcher->lock );
    }

    free( art );
    free( key );
}

static int InvokeModule(struct task *task, int scope, const char *type)
{
    if (!TaskCurrent(task)) return -EINTR;
    meta_fetcher_t *mf = vlc_custom_create(task->fetcher->owner, sizeof(*mf), type);
    if (mf == NULL) return VLC_ENOMEM;
    mf->e_scope = scope; mf->e_provider = task->provider; mf->p_item = task->item;
    const bool named = task->mode == FETCH_NAMED_PROVIDER;
    module_t *module = module_need(mf, type, named ? "lua-metadata-provider" : NULL, named);
    if (module != NULL) module_unneed(mf, module);
    vlc_object_delete(mf);
    return module != NULL ? VLC_SUCCESS : VLC_EGENERIC;
}

static int CheckMeta(input_item_t *item)
{
    vlc_mutex_lock(&item->lock);
    const bool error = item->p_meta == NULL
        || vlc_meta_Get(item->p_meta, vlc_meta_Title) == NULL
        || vlc_meta_Get(item->p_meta, vlc_meta_Artist) == NULL
        || vlc_meta_Get(item->p_meta, vlc_meta_Album) == NULL;
    vlc_mutex_unlock(&item->lock); return error;
}

static int CheckArt(input_item_t *item)
{
    vlc_mutex_lock(&item->lock);
    const bool error = item->p_meta == NULL || vlc_meta_Get(item->p_meta, vlc_meta_ArtworkURL) == NULL;
    vlc_mutex_unlock(&item->lock); return error;
}

static bool TaskEnter(struct task *task)
{
    if (!TaskCurrent(task)) return false;
    if (task->legacy != NULL) {
        if (!vlc_metadata_legacy_Begin(task->legacy)) return false;
        task->legacy_running = true;
    }
    vlc_interrupt_t *interrupt = task->legacy != NULL ? vlc_metadata_legacy_Interrupt(task->legacy) : &task->interrupt;
    task->previous_interrupt = vlc_interrupt_set(interrupt);
    task->interrupt_attached = true;
    return TaskCurrent(task);
}

static void TaskLeave(struct task *task)
{
    if (task->interrupt_attached) {
        vlc_interrupt_set(task->previous_interrupt); task->interrupt_attached = false;
    }
    if (task->legacy_running) { task->legacy_running = false; vlc_metadata_legacy_End(task->legacy); }
}

static void NotifyArtFetchEnded(struct task *task, bool fetched, int status)
{
    if (!TaskCurrent(task)) { fetched = false; status = -EINTR; }
    if (task->mode == FETCH_OWNED_LOCAL) vlc_metadata_pipeline_stage_Done(task->local, status);
    else if (task->mode == FETCH_NAMED_PROVIDER) task->probe(task->item, status, task->userdata);
    else if (task->cbs != NULL && task->cbs->on_art_fetch_ended != NULL)
        task->cbs->on_art_fetch_ended(task->item, fetched, task->userdata);
}

static void TaskFinish(struct task *task, vlc_executor_t *next, bool fetched, int status)
{
    TaskLeave(task);
    if (next != NULL) {
        status = Submit(task->fetcher, next, task->item, task->options, task->cbs, task->userdata,
            task->key, task->mode, task->provider, task->local, task->job, task->probe, task);
        if (status == VLC_SUCCESS) {
            FetcherRemoveTask(task->fetcher, task); TaskDelete(task); return;
        }
    }
    FetcherRemoveTask(task->fetcher, task);
    NotifyArtFetchEnded(task, fetched, status); TaskDelete(task);
}

static int SearchByScope(struct task *task, int scope, vlc_executor_t **next)
{
    input_fetcher_t *fetcher = task->fetcher; input_item_t *item = task->item;
    if (!TaskCurrent(task)) return -EINTR;
    if (vlc_artwork_item_IsRestricted(item)) {
        char *approved = vlc_artwork_item_GetURI(item);
        if (approved == NULL) return VLC_ENOENT;
        free(approved);
        if (task->mode == FETCH_LEGACY) *next = fetcher->executor_downloader;
        return VLC_SUCCESS;
    }
    if (CheckMeta(item)) InvokeModule(task, scope, "meta fetcher");
    if (!TaskCurrent(task)) return -EINTR;
    bool found = !CheckArt(item);
    if (!found && TaskCurrent(task)) found = !ReadAlbumCache(fetcher, item);
    if (!found && TaskCurrent(task)) found = !input_FindArtInCacheUsingItemUID(item);
    if (!found && TaskCurrent(task)) found = !input_FindArtInCache(item);
    if (!found && TaskCurrent(task)) { InvokeModule(task, scope, "art finder"); found = !CheckArt(item); }
    if (!TaskCurrent(task)) return -EINTR;
    if (!found) return VLC_ENOENT;
    AddAlbumCache(fetcher, item, false);
    if (task->mode == FETCH_LEGACY) *next = fetcher->executor_downloader;
    return VLC_SUCCESS;
}

static void RunDownloader(void *userdata)
{
    vlc_thread_set_name("vlc-run-fetcher");
    struct task *task = userdata; input_fetcher_t *fetcher = task->fetcher;
    bool fetched = false; char *arturl = NULL;
    if (!TaskEnter(task)) goto out;
    const bool restricted = vlc_artwork_item_IsRestricted(task->item);
    if (!restricted && TaskCurrent(task)) ReadAlbumCache(fetcher, task->item);
    if (!TaskCurrent(task)) goto out;
    arturl = vlc_artwork_item_GetURI(task->item);
    if (arturl == NULL) goto out;
    if (!strncasecmp(arturl, "file://", 7) || !strncasecmp(arturl, "attachment://", 13)) { fetched = true; goto out; }
    if (restricted || vlc_artwork_item_IsRestricted(task->item) || !TaskCurrent(task)) goto out;
    stream_t *source = vlc_stream_NewURL(fetcher->owner, arturl);
    if (source == NULL) goto out;
    struct vlc_memstream output; vlc_memstream_open(&output);
    while (TaskCurrent(task)) {
        char buffer[2048]; const ssize_t count = vlc_stream_Read(source, buffer, sizeof(buffer));
        if (count <= 0 || vlc_memstream_write(&output, buffer, count) < (size_t)count) break;
    }
    vlc_stream_Delete(source);
    if (vlc_memstream_close(&output)) goto out;
    if (!TaskCurrent(task)) { free(output.ptr); goto out; }
    input_SaveArt(fetcher->owner, task->item, output.ptr, output.length, NULL); free(output.ptr);
    if (!TaskCurrent(task)) goto out;
    AddAlbumCache(fetcher, task->item, true); fetched = true;
out:
    if (fetched && TaskCurrent(task)) input_item_SetArtFetched(task->item, true);
    free(arturl); TaskFinish(task, NULL, fetched, VLC_SUCCESS);
}

static void RunSearchLocal(void *userdata)
{
    vlc_thread_set_name("vlc-run-searchl");
    struct task *task = userdata; vlc_executor_t *next = NULL;
    int status = TaskEnter(task) ? SearchByScope(task, FETCHER_SCOPE_LOCAL, &next) : -EINTR;
    if (TaskCurrent(task) && status != VLC_SUCCESS && task->mode == FETCH_LEGACY
        && (task->options & VLC_PREPARSER_TYPE_FETCHMETA_NET)) next = task->fetcher->executor_network;
    if (task->mode == FETCH_OWNED_LOCAL && status == VLC_ENOENT) status = VLC_SUCCESS;
    if (next == NULL && TaskCurrent(task) && status == VLC_ENOENT) input_item_SetArtNotFound(task->item, true);
    TaskFinish(task, next, false, status);
}

static void RunSearchNetwork(void *userdata)
{
    vlc_thread_set_name("vlc-run-searchn");
    struct task *task = userdata; vlc_executor_t *next = NULL; int status = -EINTR;
    if (TaskEnter(task)) {
        if (task->mode == FETCH_NAMED_PROVIDER) {
            const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(task->job);
            if (!(request->flags & VLC_METADATA_REQUEST_NETWORK)
                || !var_InheritBool(task->fetcher->owner, "metadata-network-access")
                || vlc_artwork_item_IsRestricted(task->item)) status = -EACCES;
            else {
                status = InvokeModule(task, FETCHER_SCOPE_NETWORK, "art finder");
                if (status == VLC_SUCCESS && CheckArt(task->item)) status = VLC_ENOENT;
            }
        } else status = SearchByScope(task, FETCHER_SCOPE_NETWORK, &next);
    }
    if (next == NULL && TaskCurrent(task) && status == VLC_ENOENT) input_item_SetArtNotFound(task->item, true);
    TaskFinish(task, next, false, status);
}

input_fetcher_t* input_fetcher_New( vlc_object_t* owner,
                                    int request_type )
{
    assert(request_type & VLC_PREPARSER_TYPE_FETCHMETA_ALL);
    input_fetcher_t* fetcher = calloc(1, sizeof(*fetcher));

    if( unlikely( !fetcher ) )
        return NULL;

    int max_threads = var_InheritInteger(owner, "fetch-art-threads");
    if (max_threads < 1)
        max_threads = 1;

    if (request_type & VLC_PREPARSER_TYPE_FETCHMETA_LOCAL)
    {
        fetcher->executor_local = vlc_executor_New(max_threads);
        if (!fetcher->executor_local)
        {
            free(fetcher);
            return NULL;
        }
    }
    else
        fetcher->executor_local = NULL;

    if (request_type & VLC_PREPARSER_TYPE_FETCHMETA_NET)
    {
        fetcher->executor_network = vlc_executor_New(max_threads);
        if (!fetcher->executor_network)
        {
            if (fetcher->executor_local)
                vlc_executor_Delete(fetcher->executor_local);
            free(fetcher);
            return NULL;
        }

    }
    else
        fetcher->executor_network = NULL;

    fetcher->executor_downloader = vlc_executor_New(max_threads);
    if (!fetcher->executor_downloader)
    {
        if (fetcher->executor_network)
            vlc_executor_Delete(fetcher->executor_network);
        if (fetcher->executor_local)
            vlc_executor_Delete(fetcher->executor_local);
        free(fetcher);
        return NULL;
    }

    fetcher->owner = owner;

    vlc_atomic_rc_init(&fetcher->refs);
    vlc_mutex_init(&fetcher->lock);
    vlc_list_init(&fetcher->submitted_tasks);
    fetcher->closing = false;

    vlc_dictionary_init( &fetcher->album_cache, 0 );

    return fetcher;
}

int input_fetcher_Push(input_fetcher_t* fetcher, input_item_t* item,
                       int options,
                       const input_fetcher_callbacks_t *cbs, void *cbs_userdata)
{
    assert(options & VLC_PREPARSER_TYPE_FETCHMETA_ALL);
    if (options & VLC_PREPARSER_TYPE_FETCHMETA_LOCAL)
        assert(fetcher->executor_local != NULL);
    if (options & VLC_PREPARSER_TYPE_FETCHMETA_NET)
        assert(fetcher->executor_network != NULL);

    vlc_executor_t *executor = options & VLC_PREPARSER_TYPE_FETCHMETA_LOCAL
                             ? fetcher->executor_local
                             : fetcher->executor_network;

    return Submit(fetcher, executor, item, options, cbs, cbs_userdata, cbs_userdata,
        FETCH_LEGACY, META_FETCHER_PROVIDER_ANY, NULL, NULL, NULL, NULL);
}

int input_fetcher_PushLocal(input_fetcher_t *fetcher, vlc_metadata_pipeline_stage_t *stage)
{
    if (fetcher == NULL || stage == NULL || fetcher->executor_local == NULL) return VLC_EINVAL;
    return Submit(fetcher, fetcher->executor_local, vlc_metadata_pipeline_stage_GetItem(stage),
        VLC_PREPARSER_TYPE_FETCHMETA_LOCAL, NULL, NULL, stage, FETCH_OWNED_LOCAL,
        META_FETCHER_PROVIDER_NONE, stage, NULL, NULL, NULL);
}

int input_fetcher_Probe(input_fetcher_t *fetcher, vlc_metadata_job_t *job,
    meta_fetcher_provider_t provider, input_fetcher_probe_cb callback, void *userdata)
{
    if (fetcher == NULL || job == NULL || callback == NULL || fetcher->executor_network == NULL
        || provider < META_FETCHER_PROVIDER_MUSICBRAINZ || provider > META_FETCHER_PROVIDER_LASTFM) return VLC_EINVAL;
    const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(job);
    if (!vlc_metadata_job_IsPending(job)) return -EINTR;
    if (!(request->flags & VLC_METADATA_REQUEST_NETWORK) || vlc_artwork_item_IsRestricted(request->item)
        || !var_InheritBool(fetcher->owner, "metadata-network-access")) return -EACCES;
    input_item_t *copy = input_item_Copy(request->item);
    if (copy == NULL) return VLC_ENOMEM;
    input_item_SetArtURL(copy, NULL);
    const int status = Submit(fetcher, fetcher->executor_network, copy, VLC_PREPARSER_TYPE_FETCHMETA_NET,
        NULL, userdata, job, FETCH_NAMED_PROVIDER, provider, NULL, job, callback, NULL);
    input_item_Release(copy); return status;
}

static size_t CancelTasks(input_fetcher_t *fetcher, const void *key, bool close)
{
    struct { struct task *task; bool dequeued; } pending[MAX_TASKS];
    size_t count = 0;
    vlc_mutex_lock(&fetcher->lock);
    if (fetcher->deleting && !close) { vlc_mutex_unlock(&fetcher->lock); return 0; }
    if (close) fetcher->closing = true;
    struct task *task;
    vlc_list_foreach(task, &fetcher->submitted_tasks, node) {
        if (key != NULL && task->key != key) continue;
        assert(count < MAX_TASKS);
        atomic_store(&task->cancelled, true);
        const bool dequeued = vlc_executor_Cancel(task->executor, &task->runnable);
        pending[count].task = TaskHold(task); pending[count++].dequeued = dequeued;
        if (dequeued) { vlc_list_remove(&task->node); task->listed = false; }
    }
    vlc_mutex_unlock(&fetcher->lock);
    // Complete all executor mutations before a callback can call Delete.
    for (size_t i = 0; i < count; ++i) {
        vlc_interrupt_kill(&pending[i].task->interrupt);
        vlc_metadata_legacy_Cancel(pending[i].task->legacy);
    }
    for (size_t i = 0; i < count; ++i) {
        task = pending[i].task;
        if (pending[i].dequeued) { NotifyArtFetchEnded(task, false, -EINTR); TaskDelete(task); }
        TaskDelete(task);
    }
    return count;
}

size_t input_fetcher_Cancel(input_fetcher_t *fetcher, const void *key)
{
    input_fetcher_Hold(fetcher); const size_t count = CancelTasks(fetcher, key, false);
    input_fetcher_Release(fetcher); return count;
}

void input_fetcher_Delete( input_fetcher_t* fetcher )
{
    vlc_mutex_lock(&fetcher->lock);
    if (fetcher->deleting) { vlc_mutex_unlock(&fetcher->lock); return; }
    fetcher->deleting = true; vlc_mutex_unlock(&fetcher->lock);
    CancelTasks(fetcher, NULL, true);

    if (fetcher->executor_local)
        vlc_executor_Delete(fetcher->executor_local);
    if (fetcher->executor_network)
        vlc_executor_Delete(fetcher->executor_network);
    assert(fetcher->executor_downloader);
    vlc_executor_Delete(fetcher->executor_downloader);

    vlc_dictionary_clear( &fetcher->album_cache, FreeCacheEntry, NULL );
    input_fetcher_Release(fetcher);
}
