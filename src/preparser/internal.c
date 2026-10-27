/*****************************************************************************
 * internal.c
 *****************************************************************************
 * Copyright © 2017-2025 VLC authors and VideoLAN
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

#include <fcntl.h>

#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_executor.h>
#include <vlc_preparser.h>
#include <vlc_interrupt.h>
#include <vlc_fs.h>

#include "preparser.h"
#include "input/input_interface.h"
#include "input/input_internal.h"
#include "fetcher.h"
#include "metadata_request_events.h"
#include <vlc_artwork_policy.h>

union vlc_preparser_cbs_internal
{
    const struct vlc_preparser_cbs *parser;
    const struct vlc_thumbnailer_cbs *thumbnailer;
    const struct vlc_thumbnailer_to_files_cbs *thumbnailer_to_files;
};

struct preparser_sys
{
    vlc_object_t* owner;
    input_fetcher_t* fetcher;
    vlc_executor_t *parser;
    vlc_executor_t *thumbnailer;
    vlc_executor_t *thumbnailer_to_files;
    vlc_tick_t timeout;
    vlc_atomic_rc_t refs;
    vlc_metadata_pipeline_t *pipeline;
    struct vlc_preparser_parse_engine engine;
    bool closing;


    vlc_mutex_t lock;
    struct vlc_list submitted_tasks; /**< list of struct task */
};

struct task_thumbnail_output
{
    vlc_fourcc_t fourcc;
    int width;
    int height;
    bool crop;
    char *file_path;
    unsigned int creat_mode;
};

struct vlc_preparser_req_owner
{
    struct preparser_sys *preparser;
    input_item_t *item;
    int options;
    struct vlc_thumbnailer_arg thumb_arg;
    union vlc_preparser_cbs_internal cbs;
    void *userdata;

    vlc_interrupt_t *i11e_ctx;
    picture_t *pic;
    struct task_thumbnail_output *outputs;
    size_t output_count;

    vlc_sem_t preparse_ended;
    int preparse_status;
    atomic_bool interrupted;
    vlc_metadata_pipeline_guard_t *guard;
    vlc_metadata_pipeline_stage_t *stage;
    input_item_t *working;
    struct vlc_metadata_request_events events;
    bool listed, cancel_requested;


    struct vlc_runnable runnable; /**< to be passed to the executor */
    vlc_executor_t *executor; /**< executor the runnable was last submitted to */

    struct vlc_list node; /**< node of vlc_preparser_t.submitted_tasks */

    vlc_atomic_rc_t rc;

    struct vlc_preparser_req req;
};

static struct vlc_preparser_req *PreparserRequestRetain(struct vlc_preparser_req *);
static void Interrupt(struct vlc_preparser_req *);
static struct preparser_sys *PreparserHold(struct preparser_sys *preparser)
{ vlc_atomic_rc_inc(&preparser->refs); return preparser; }
static void PreparserRelease(struct preparser_sys *preparser)
{ if (vlc_atomic_rc_dec(&preparser->refs)) free(preparser); }

static struct vlc_preparser_req_owner *
preparser_req_get_owner(struct vlc_preparser_req *req)
{
    return container_of(req, struct vlc_preparser_req_owner, req);
}

static input_item_t *
preparser_req_GetItem(struct vlc_preparser_req *req)
{
    assert(req != NULL);
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    return req_owner->item;
}

bool vlc_preparser_internal_HasRequest(void *opaque, const struct vlc_preparser_req *req)
{
    return req != NULL && req->ops->get_item == preparser_req_GetItem
        && container_of(req, const struct vlc_preparser_req_owner, req)->preparser == opaque;
}

static void
PreparserRequestDelete(struct vlc_preparser_req *req)
{
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    input_item_Release(req_owner->item);
    for (size_t i = 0; i < req_owner->output_count; ++i)
        free(req_owner->outputs[i].file_path);
    free(req_owner->outputs);
    if (req_owner->pic != NULL)
        picture_Release(req_owner->pic);
    if (req_owner->i11e_ctx != NULL)
        vlc_interrupt_destroy(req_owner->i11e_ctx);
    vlc_metadata_pipeline_guard_Release(req_owner->guard);
    vlc_metadata_request_events_Clear(&req_owner->events);
    PreparserRelease(req_owner->preparser);
    free(req_owner);
}

static void
preparser_req_Release(struct vlc_preparser_req *req)
{
    assert(req != NULL);
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    if (!vlc_atomic_rc_dec(&req_owner->rc))
        return;

    PreparserRequestDelete(req);
}

static struct vlc_preparser_req *
PreparserRequestNew(struct preparser_sys *preparser, void (*run)(void *), input_item_t *item,
                    int options, const struct vlc_thumbnailer_arg *thumb_arg,
                    union vlc_preparser_cbs_internal cbs, void *userdata)
{
    struct vlc_preparser_req_owner *req_owner = malloc(sizeof(*req_owner));
    if (!req_owner)
        return NULL;

    req_owner->preparser = PreparserHold(preparser);
    req_owner->guard = NULL; req_owner->stage = NULL; req_owner->working = NULL;
    req_owner->events = (struct vlc_metadata_request_events){0};
    req_owner->listed = req_owner->cancel_requested = false;
    req_owner->item = item;
    req_owner->options = options;
    req_owner->cbs = cbs;
    req_owner->userdata = userdata;
    req_owner->pic = NULL;
    req_owner->outputs = NULL;
    req_owner->output_count = 0;
    vlc_atomic_rc_init(&req_owner->rc);

    static const struct vlc_preparser_req_operations ops = {
        .get_item = preparser_req_GetItem,
        .release = preparser_req_Release,
    };
    req_owner->req.ops = &ops;
    req_owner->req.submitted = false;

    if (thumb_arg == NULL)
        req_owner->thumb_arg = (struct vlc_thumbnailer_arg) {
            .seek.type = VLC_THUMBNAILER_SEEK_NONE,
            .hw_dec = false,
        };
    else
        req_owner->thumb_arg = *thumb_arg;

    input_item_Hold(item);

    vlc_sem_init(&req_owner->preparse_ended, 0);
    req_owner->preparse_status = options & VLC_PREPARSER_TYPE_PARSE ? VLC_EGENERIC : VLC_SUCCESS;
    atomic_init(&req_owner->interrupted, false);

    req_owner->runnable.run = run;
    req_owner->runnable.userdata = &req_owner->req;
    req_owner->executor = NULL;
    if ((options & VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES)
        || ((options & VLC_PREPARSER_TYPE_PARSE) && preparser->engine.run != NULL))
        req_owner->i11e_ctx = vlc_interrupt_create();
    else
        req_owner->i11e_ctx = NULL;
    if (((options & VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES)
        || ((options & VLC_PREPARSER_TYPE_PARSE) && preparser->engine.run != NULL))
        && req_owner->i11e_ctx == NULL) { PreparserRequestDelete(&req_owner->req); return NULL; }

    return &req_owner->req;
}

static void
PreparserQueueTaskLocked(struct vlc_preparser_req_owner *req_owner,
                         vlc_executor_t *executor)
{
    req_owner->executor = executor;
    vlc_executor_Submit(executor, &req_owner->runnable);
}

static bool
PreparserSubmitTask(struct preparser_sys *preparser,
                    struct vlc_preparser_req *req,
                    vlc_executor_t *executor)
{
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    vlc_mutex_lock(&preparser->lock);
    if (preparser->closing) { vlc_mutex_unlock(&preparser->lock); return false; }
    vlc_list_append(&req_owner->node, &preparser->submitted_tasks);
    req_owner->listed = true;
    PreparserQueueTaskLocked(req_owner, executor);
    vlc_mutex_unlock(&preparser->lock); return true;
}

static void
PreparserRemoveTask(struct preparser_sys *preparser, struct vlc_preparser_req *req)
{
    vlc_mutex_lock(&preparser->lock);
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    if (req_owner->listed) { vlc_list_remove(&req_owner->node); req_owner->listed = false; }
    vlc_mutex_unlock(&preparser->lock);
}

static void OnRequestEnded(input_item_t *item, int status,
    struct vlc_metadata_local_events *output, void *opaque)
{
    struct vlc_preparser_req *req = opaque;
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    assert(item == request->item);
    struct vlc_metadata_local_events events = *output;
    *output = (struct vlc_metadata_local_events){0};
    if (status == VLC_SUCCESS) {
        for (size_t i = 0; i < events.subtree_count; ++i) {
            input_item_node_t *tree = events.subtrees[i];
            input_item_Release(tree->p_item); tree->p_item = input_item_Hold(item);
            if (request->cbs.parser->on_subtree_added != NULL)
                request->cbs.parser->on_subtree_added(req, tree, request->userdata);
            else input_item_node_Delete(tree);
        }
        events.subtree_count = 0;
        if (events.attachment_count != 0 && request->cbs.parser->on_attachments_added != NULL)
            request->cbs.parser->on_attachments_added(req, events.attachments,
                events.attachment_count, request->userdata);
        status = request->preparse_status;
    }
    vlc_metadata_local_events_Clear(&events);
    request->cbs.parser->on_ended(req, status, request->userdata);
    /* Keep the request visible through the final callback. */
    PreparserRemoveTask(request->preparser, req);
    vlc_preparser_req_Release(req);
}

static void
OnParserEnded(input_item_t *item, int status, void *req_)
{
    VLC_UNUSED(item);
    struct vlc_preparser_req *req = req_;
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);

    req_owner->preparse_status = status;
    vlc_sem_post(&req_owner->preparse_ended);
}

static void
OnParserSubtreeAdded(input_item_t *item, input_item_node_t *subtree, void *opaque)
{
    (void)item;
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(opaque);
    if (atomic_load(&request->interrupted)) input_item_node_Delete(subtree);
    else vlc_metadata_request_events_AddTree(&request->events, subtree);
}
static void
OnParserAttachmentsAdded(input_item_t *item, input_attachment_t *const *array,
    size_t count, void *opaque)
{
    (void)item;
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(opaque);
    if (!atomic_load(&request->interrupted))
        vlc_metadata_request_events_AddAttachments(&request->events, array, count);
}

static void
Parse(struct vlc_preparser_req *req, vlc_tick_t deadline)
{
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);

    static const input_item_parser_cbs_t cbs = {
        .on_ended = OnParserEnded,
        .on_subtree_added = OnParserSubtreeAdded,
        .on_attachments_added = OnParserAttachmentsAdded,
    };

    vlc_object_t *obj = req_owner->preparser->owner;
    const struct input_item_parser_cfg cfg = {
        .cbs = &cbs,
        .cbs_data = req,
        .subitems = req_owner->options & VLC_PREPARSER_OPTION_SUBITEMS,
        .interact = req_owner->options & VLC_PREPARSER_OPTION_INTERACT,
    };
    if (req_owner->preparser->engine.run != NULL) {
        req_owner->preparse_status = req_owner->preparser->engine.run(
            req_owner->preparser->engine.opaque, req_owner->working, &cfg,
            deadline, req_owner->i11e_ctx);
        return;
    }
    input_item_parser_id_t *parser =
        input_item_Parse(obj, req_owner->working, &cfg);
    if (parser == NULL)
    {
        req_owner->preparse_status = VLC_EGENERIC;
        return;
    }

    /* Wait until the end of parsing */
    if (deadline == VLC_TICK_INVALID)
        vlc_sem_wait(&req_owner->preparse_ended);
    else
        if (vlc_sem_timedwait(&req_owner->preparse_ended, deadline))
        {
            input_item_parser_id_Release(parser);
            req_owner->preparse_status = VLC_ETIMEOUT;
            return;
        }

    /* This call also interrupts the parsing if it is still running */
    input_item_parser_id_Release(parser);
}

static void FinishNativeStage(struct vlc_preparser_req *req, int status)
{
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    struct preparser_sys *preparser = request->preparser;
    vlc_mutex_lock(&preparser->lock);
    vlc_metadata_pipeline_stage_t *stage = request->stage; request->stage = NULL;
    vlc_mutex_unlock(&preparser->lock);
    assert(stage != NULL);
    if (atomic_load(&request->interrupted)) status = -EINTR;
    if (status == VLC_SUCCESS && request->events.status != VLC_SUCCESS) status = request->events.status;
    if (status == VLC_SUCCESS)
        status = vlc_metadata_pipeline_stage_PrepareLocal(stage, &request->events.output);
    vlc_metadata_request_events_Clear(&request->events);
    vlc_metadata_pipeline_stage_Done(stage, status);
    vlc_preparser_req_Release(req); /* native provider reference */
}
static void OnLegacyFetchEnded(input_item_t *item, int status, void *opaque)
{
    struct vlc_preparser_req *req = opaque;
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    /* A legacy lease can end at an owner transition. Its separate copy cannot
     * alter the local result after that transition. */
    if (status == VLC_SUCCESS && !atomic_load(&request->interrupted)) {
        char *artwork = input_item_GetArtURL(item);
        input_item_SetArtURL(request->working, artwork); free(artwork);
        if (input_item_IsArtFetched(item)) input_item_SetArtFetched(request->working, true);
        vlc_mutex_lock(&item->lock);
        const bool not_found = item->p_meta != NULL && (vlc_meta_GetStatus(item->p_meta) & ITEM_ART_NOTFOUND);
        vlc_mutex_unlock(&item->lock);
        if (not_found) input_item_SetArtNotFound(request->working, true);
    }
    FinishNativeStage(req, status == VLC_ENOMEM ? status : VLC_SUCCESS);
}
static void OnLocalFetchEnded(input_item_t *item, int status, void *opaque)
{
    (void)item;
    struct vlc_preparser_req *req = opaque;
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    if (status == -EINTR || status == VLC_ENOMEM) { FinishNativeStage(req, status); return; }
    /* A native local file or attachment needs no downloader. Keep the native
     * fetched status without a network read or a new artwork approval. */
    char *artwork = vlc_artwork_item_GetURI(request->working);
    if (artwork != NULL && (!strncasecmp(artwork, "file://", 7)
        || !strncasecmp(artwork, "attachment://", 13))) input_item_SetArtFetched(request->working, true);
    free(artwork);
    if (vlc_metadata_pipeline_stage_LegacyAllowed(request->stage)
        && vlc_metadata_pipeline_stage_IsCurrent(request->stage)) {
        input_item_t *copy = input_item_Copy(request->working);
        if (copy == NULL) { FinishNativeStage(req, VLC_ENOMEM); return; }
        const int started = input_fetcher_PushNative(request->preparser->fetcher,
            request->stage, copy, true, OnLegacyFetchEnded, req);
        input_item_Release(copy);
        if (started == VLC_SUCCESS) return;
        if (started == VLC_ENOMEM) { FinishNativeStage(req, started); return; }
    }
    FinishNativeStage(req, VLC_SUCCESS);
}
static void ParserRun(void *userdata)
{
    vlc_thread_set_name("vlc-run-prepars");
    struct vlc_preparser_req *req = userdata;
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    struct preparser_sys *preparser = request->preparser;
    vlc_mutex_lock(&preparser->lock); request->executor = NULL; vlc_mutex_unlock(&preparser->lock);
    const vlc_tick_t deadline = preparser->timeout ? vlc_tick_now() + preparser->timeout : VLC_TICK_INVALID;
    if (!atomic_load(&request->interrupted) && (request->options & VLC_PREPARSER_TYPE_PARSE)) {
        Parse(req, deadline);
        if (request->preparse_status == VLC_SUCCESS) input_item_SetPreparsed(request->working);
    }
    if (atomic_load(&request->interrupted) || request->preparse_status == VLC_ETIMEOUT
        || request->preparse_status == VLC_ENOMEM || request->events.status != VLC_SUCCESS) {
        FinishNativeStage(req, atomic_load(&request->interrupted) ? -EINTR
            : request->events.status != VLC_SUCCESS ? request->events.status : request->preparse_status);
        return;
    }
    if (preparser->fetcher != NULL && (request->options & VLC_PREPARSER_TYPE_FETCHMETA_ALL)) {
        const int status = input_fetcher_PushNative(preparser->fetcher, request->stage,
            request->working, false, OnLocalFetchEnded, req);
        if (status == VLC_SUCCESS) return;
        if (status == VLC_ENOMEM || status == -EINTR) { FinishNativeStage(req, status); return; }
    }
    FinishNativeStage(req, VLC_SUCCESS);
}
static int StartNativeStage(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{
    struct preparser_sys *preparser = opaque;
    struct vlc_preparser_req *req = (struct vlc_preparser_req *)vlc_metadata_pipeline_stage_GetKey(stage);
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    vlc_mutex_lock(&preparser->lock);
    if (preparser->closing || atomic_load(&request->interrupted)
        || !vlc_metadata_pipeline_stage_IsCurrent(stage)) { vlc_mutex_unlock(&preparser->lock); return -EINTR; }
    PreparserRequestRetain(req); request->stage = stage;
    request->working = vlc_metadata_pipeline_stage_GetItem(stage);
    PreparserQueueTaskLocked(request, preparser->parser);
    vlc_mutex_unlock(&preparser->lock); return VLC_SUCCESS;
}
static void CancelNativeStage(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{
    struct preparser_sys *preparser = opaque;
    struct vlc_preparser_req *req = (struct vlc_preparser_req *)vlc_metadata_pipeline_stage_GetKey(stage);
    PreparserRequestRetain(req);
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    vlc_mutex_lock(&preparser->lock);
    const bool dequeued = request->stage == stage && request->executor != NULL
        && vlc_executor_Cancel(request->executor, &request->runnable);
    if (dequeued) request->executor = NULL;
    atomic_store(&request->interrupted, true);
    vlc_mutex_unlock(&preparser->lock);
    Interrupt(req);
    if (preparser->fetcher != NULL) input_fetcher_Cancel(preparser->fetcher, stage);
    if (dequeued) FinishNativeStage(req, -EINTR);
    vlc_preparser_req_Release(req);
}

static void RetainRequestKey(void *opaque, const void *key)
{ (void)opaque; PreparserRequestRetain((struct vlc_preparser_req *)key); }
static void ReleaseRequestKey(void *opaque, const void *key)
{ (void)opaque; vlc_preparser_req_Release((struct vlc_preparser_req *)key); }

static bool
on_thumbnailer_input_event( input_thread_t *input,
                            const struct vlc_input_event *event, void *userdata )
{
    VLC_UNUSED(input);
    if ( event->type != INPUT_EVENT_THUMBNAIL_READY &&
         ( event->type != INPUT_EVENT_STATE || ( event->state.value != ERROR_S &&
                                                 event->state.value != END_S ) ) )
         return false;

    struct vlc_preparser_req *req = userdata;
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);

    if (event->type == INPUT_EVENT_THUMBNAIL_READY)
    {
        req_owner->pic = picture_Hold(event->thumbnail);
        req_owner->preparse_status = VLC_SUCCESS;
    }
    vlc_sem_post(&req_owner->preparse_ended);
    return true;
}

static int
WriteToFile(const block_t *block, const char *path, unsigned mode)
{
    int bflags =
#ifdef O_BINARY
        O_BINARY;
#else
        0;
#endif

    int fd = vlc_open(path, O_WRONLY|O_CREAT|O_TRUNC|O_NONBLOCK|bflags, mode);
    if (fd == -1)
        return -errno;

    uint8_t *data = block->p_buffer;
    size_t size = block->i_buffer;

    while (size > 0)
    {
        ssize_t len = vlc_write_i11e(fd, data, size);
        if (len == -1)
        {
            if (errno == EAGAIN)
                continue;
            break;
        }

        size -= len;
        data += len;
    }

    close(fd);
    return size == 0 ? 0 : -errno;
}

static void
ThumbnailerToFilesRun(void *userdata)
{
    vlc_thread_set_name("vlc-run-thfil");

    struct vlc_preparser_req *req = userdata;
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    struct preparser_sys *preparser = req_owner->preparser;
    picture_t* pic = req_owner->pic;

    vlc_interrupt_set(req_owner->i11e_ctx);

    bool *result_array = vlc_alloc(req_owner->output_count, sizeof(bool));
    if (result_array == NULL)
        goto error;

    for (size_t i = 0; i < req_owner->output_count; ++i)
    {
        struct task_thumbnail_output *output = &req_owner->outputs[i];

        if (output->fourcc == VLC_CODEC_UNKNOWN)
        {
            result_array[i] = false;
            continue;
        }

        block_t* block;
        int ret = picture_Export(preparser->owner, &block, NULL, pic,
                                 output->fourcc, output->width, output->height,
                                 output->crop);

        if (ret != VLC_SUCCESS)
        {
            result_array[i] = false;
            continue;
        }

        ret = WriteToFile(block, output->file_path, output->creat_mode);
        block_Release(block);
        if (ret == -EINTR)
        {
            req_owner->preparse_status = -EINTR;
            goto error;
        }

        result_array[i] = ret == 0;
    }

    req_owner->preparse_status = VLC_SUCCESS;
error:
    PreparserRemoveTask(preparser, req);
    if (req_owner->preparse_status == VLC_SUCCESS)
        req_owner->cbs.thumbnailer_to_files->on_ended(req, req_owner->preparse_status,
                                                      result_array,
                                                      req_owner->output_count,
                                                      req_owner->userdata);
    else
        req_owner->cbs.thumbnailer_to_files->on_ended(req, req_owner->preparse_status,
                                                      NULL, 0, req_owner->userdata);
    req_owner->pic = NULL;
    picture_Release(pic);
    vlc_preparser_req_Release(req);
    free(result_array);
}

static void
ThumbnailerRun(void *userdata)
{
    vlc_thread_set_name("vlc-run-thumb");

    struct vlc_preparser_req *req = userdata;
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    struct preparser_sys *preparser = req_owner->preparser;

    static const struct vlc_input_thread_callbacks cbs = {
        .on_event = on_thumbnailer_input_event,
    };

    const struct vlc_input_thread_cfg cfg = {
        .type = INPUT_TYPE_THUMBNAILING,
        .hw_dec = req_owner->thumb_arg.hw_dec ? INPUT_CFG_HW_DEC_ENABLED
                                        : INPUT_CFG_HW_DEC_DISABLED,
        .cbs = &cbs,
        .cbs_data = req,
    };

    vlc_tick_t deadline = preparser->timeout != VLC_TICK_INVALID ?
                          vlc_tick_now() + preparser->timeout :
                          VLC_TICK_INVALID;

    input_thread_t* input =
            input_Create( preparser->owner, req_owner->item, &cfg );
    if (!input)
        goto error;

    assert(req_owner->thumb_arg.seek.speed == VLC_THUMBNAILER_SEEK_PRECISE
        || req_owner->thumb_arg.seek.speed == VLC_THUMBNAILER_SEEK_FAST);
    bool fast_seek = req_owner->thumb_arg.seek.speed == VLC_THUMBNAILER_SEEK_FAST;

    switch (req_owner->thumb_arg.seek.type)
    {
        case VLC_THUMBNAILER_SEEK_NONE:
            break;
        case VLC_THUMBNAILER_SEEK_TIME:
            if (req_owner->thumb_arg.seek.time >= VLC_TICK_0)
                input_SetTime(input, req_owner->thumb_arg.seek.time, fast_seek);
            break;
        case VLC_THUMBNAILER_SEEK_POS:
            if (req_owner->thumb_arg.seek.pos > 0)
            {
                float pos = req_owner->thumb_arg.seek.pos;
                if (pos > 1)
                    pos = 1;
                input_SetPosition(input, pos, fast_seek);
            }
            break;
        default:
            vlc_assert_unreachable();
    }

    int ret = input_Start(input);
    if (ret != VLC_SUCCESS)
    {
        input_Close(input);
        goto error;
    }

    bool timeout = false;
    if (deadline == VLC_TICK_INVALID)
        vlc_sem_wait(&req_owner->preparse_ended);
    else
        timeout = vlc_sem_timedwait(&req_owner->preparse_ended, deadline) != 0;

    /* tear the input thread down before reading what it produced to avoid races */
    input_Stop(input);
    input_Close(input);

    if (timeout)
        req_owner->preparse_status = VLC_ETIMEOUT;
    if (atomic_load(&req_owner->interrupted))
        req_owner->preparse_status = -EINTR;

    picture_t* pic = req_owner->pic;

    if (req_owner->options & VLC_PREPARSER_TYPE_THUMBNAIL)
    {
        assert((req_owner->options & VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES) == 0);

        PreparserRemoveTask(preparser, req);
        req_owner->cbs.thumbnailer->on_ended(req, req_owner->preparse_status,
                                             req_owner->preparse_status == VLC_SUCCESS ?
                                             pic : NULL, req_owner->userdata);
    }
    else
    {
        assert(req_owner->options & VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES);

        bool queued = false;

        if (req_owner->preparse_status == VLC_SUCCESS)
        {
            /* Export the thumbnail to several files via a new executor in
             * order to not slow down the current thread doing picture
             * conversion and I/O */

            assert(pic != NULL);

            /* the request stays published while its runnable moves to
               another executor. Re-queue it under the lock so that a
               concurrent cancel never targets the executor it just left. */
            vlc_mutex_lock(&preparser->lock);
            if (atomic_load(&req_owner->interrupted))
                req_owner->preparse_status = -EINTR;
            else
            {
                req_owner->runnable.run = ThumbnailerToFilesRun;
                PreparserQueueTaskLocked(req_owner, preparser->thumbnailer_to_files);
                queued = true;
            }
            vlc_mutex_unlock(&preparser->lock);
        }

        if (queued)
        {
            pic = NULL;
            req_owner = NULL;
        }
        else
        {
            PreparserRemoveTask(preparser, req);
            req_owner->cbs.thumbnailer_to_files->on_ended(req, req_owner->preparse_status,
                                                          NULL, 0, req_owner->userdata);
        }
    }

    if (pic)
    {
        req_owner->pic = NULL;
        picture_Release(pic);
    }

error:
    if (req_owner != NULL)
        vlc_preparser_req_Release(req);
}

static void
Interrupt(struct vlc_preparser_req *req)
{
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    atomic_store(&req_owner->interrupted, true);
    if (req_owner->i11e_ctx != NULL)
        vlc_interrupt_kill(req_owner->i11e_ctx);

    vlc_sem_post(&req_owner->preparse_ended);
}

static struct vlc_preparser_req *
PreparserRequestRetain(struct vlc_preparser_req *req)
{
    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    vlc_atomic_rc_inc(&req_owner->rc);
    return req;
}

static vlc_preparser_req *
preparser_req_NewParse( void *opaque, input_item_t *item,
                        int type_options,
                        const struct vlc_preparser_cbs *cbs,
                        void *cbs_userdata )
{
    assert(opaque != NULL);
    struct preparser_sys *preparser = opaque;

    assert((type_options & VLC_PREPARSER_TYPE_THUMBNAIL) == 0);
    assert((type_options & VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES) == 0);

    assert(type_options & VLC_PREPARSER_TYPE_PARSE
        || type_options & VLC_PREPARSER_TYPE_FETCHMETA_ALL);

    assert(!(type_options & VLC_PREPARSER_TYPE_PARSE)
        || preparser->parser != NULL);
    assert(!(type_options & VLC_PREPARSER_TYPE_FETCHMETA_ALL)
        || preparser->fetcher != NULL);

    assert(cbs != NULL && cbs->on_ended != NULL);

    union vlc_preparser_cbs_internal req_cbs = {
        .parser = cbs,
    };

    return PreparserRequestNew(preparser, ParserRun, item, type_options,
                               NULL, req_cbs, cbs_userdata);
}

static vlc_preparser_req *
preparser_req_NewThumbnail( void *opaque, input_item_t *item,
                            const struct vlc_thumbnailer_arg *thumb_arg,
                            const struct vlc_thumbnailer_cbs *cbs,
                            void *cbs_userdata )
{
    assert(opaque != NULL);
    struct preparser_sys *preparser = opaque;

    assert(preparser->thumbnailer != NULL);
    assert(cbs != NULL && cbs->on_ended != NULL);

    union vlc_preparser_cbs_internal req_cbs = {
        .thumbnailer = cbs,
    };

    return PreparserRequestNew(preparser, ThumbnailerRun, item, VLC_PREPARSER_TYPE_THUMBNAIL,
                               thumb_arg, req_cbs, cbs_userdata);
}

static int
CheckThumbnailerFormat(enum vlc_thumbnailer_format format,
                       enum vlc_thumbnailer_format *out_format,
                       const char **out_ext, vlc_fourcc_t *out_fourcc)
{
    static const struct format
    {
        enum vlc_thumbnailer_format format;
        const char *module;
        vlc_fourcc_t fourcc;
        const char *ext;
    } formats[] = {
        {
            .format = VLC_THUMBNAILER_FORMAT_PNG, .fourcc = VLC_CODEC_PNG,
            .module = "png", .ext = "png",
        },
        {
            .format = VLC_THUMBNAILER_FORMAT_WEBP, .fourcc = VLC_CODEC_WEBP,
            .module = "vpx", .ext = "webp",
        },
        {
            .format = VLC_THUMBNAILER_FORMAT_JPEG, .fourcc = VLC_CODEC_JPEG,
            .module = "jpeg", .ext = "jpg",
        },
        {
            .format = VLC_THUMBNAILER_FORMAT_RGBA, .fourcc = VLC_CODEC_RGBA,
            .module = "avcodec", .ext = "rgba",
        },
        {
            .format = VLC_THUMBNAILER_FORMAT_ARGB, .fourcc = VLC_CODEC_ARGB,
            .module = "avcodec", .ext = "argb",
        },
    };

    for (size_t i = 0; i < ARRAY_SIZE(formats); ++i)
    {
        /* if out_format is valid, don't check format, and get the first
         * possible format */
        if (out_format == NULL && format != formats[i].format)
            continue;

        /* Check if the image encoder is present */
        if (!module_exists(formats[i].module))
            continue;

        if (out_format != NULL)
            *out_format = formats[i].format;
        if (out_fourcc != NULL)
            *out_fourcc = formats[i].fourcc;
        if (out_ext != NULL)
            *out_ext = formats[i].ext;
        return 0;
    }

    return VLC_ENOENT;
}

int
vlc_preparser_GetBestThumbnailerFormat(enum vlc_thumbnailer_format *format,
                                       const char **out_ext)
{
    return CheckThumbnailerFormat(0, format, out_ext, NULL);
}

int
vlc_preparser_CheckThumbnailerFormat(enum vlc_thumbnailer_format format)
{
    return CheckThumbnailerFormat(format, NULL, NULL, NULL);
}

static vlc_preparser_req *
preparser_req_NewThumbnailToFiles( void *opaque, input_item_t *item,
                                   const struct vlc_thumbnailer_arg *thumb_arg,
                                   const struct vlc_thumbnailer_output *outputs,
                                   size_t output_count,
                                   const struct vlc_thumbnailer_to_files_cbs *cbs,
                                   void *cbs_userdata )
{
    assert(opaque != NULL);
    struct preparser_sys *preparser = opaque;

    assert(preparser->thumbnailer != NULL);
    assert(preparser->thumbnailer_to_files != NULL);
    assert(cbs != NULL && cbs->on_ended != NULL);
    assert(outputs != NULL && output_count > 0);

    union vlc_preparser_cbs_internal req_cbs = {
        .thumbnailer_to_files = cbs,
    };

    struct vlc_preparser_req *req =
        PreparserRequestNew(preparser, ThumbnailerRun, item,
                            VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES, thumb_arg,
                            req_cbs, cbs_userdata);
    if (req == NULL)
        return NULL;

    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);

    req_owner->outputs = vlc_alloc(output_count, sizeof(*outputs));
    if (unlikely(req_owner->outputs == NULL))
    {
        PreparserRequestDelete(req);
        return NULL;
    }

    size_t valid_output_count = 0;
    for (size_t i = 0; i < output_count; ++i)
    {
        struct task_thumbnail_output *dst = &req_owner->outputs[i];
        const struct vlc_thumbnailer_output *src = &outputs[i];
        assert(src->file_path != NULL);

        enum vlc_thumbnailer_format format = src->format;
        int ret = CheckThumbnailerFormat(format, NULL, NULL, &dst->fourcc);
        if (ret != 0)
            dst->fourcc = VLC_CODEC_UNKNOWN;
        else
            valid_output_count++;

        dst->width = src->width;
        dst->height = src->height;
        dst->crop = src->crop;
        dst->creat_mode = src->creat_mode;
        dst->file_path = strdup(src->file_path);

        if (unlikely(dst->file_path == NULL))
        {
            PreparserRequestDelete(req);
            return NULL;
        }
        req_owner->output_count++;
    }

    if (valid_output_count == 0)
    {
        PreparserRequestDelete(req);
        msg_Err(preparser->owner, "thumbnailer: no valid \"image encoder\" found");
        return NULL;
    }

    return req;
}

static int
preparser_Submit( void *opaque, struct vlc_preparser_req *req )
{
    assert(opaque != NULL);
    struct preparser_sys *preparser = opaque;

    struct vlc_preparser_req_owner *req_owner = preparser_req_get_owner(req);
    assert(req_owner->preparser == preparser);

    /* the task's reference */
    PreparserRequestRetain(req);

    if (req_owner->options & (VLC_PREPARSER_TYPE_THUMBNAIL |
                              VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES))
    {
        assert(preparser->thumbnailer != NULL);
        if (PreparserSubmitTask(preparser, req, preparser->thumbnailer)) return VLC_SUCCESS;
        vlc_preparser_req_Release(req); return -EINTR;
    }

    vlc_metadata_pipeline_guard_Release(req_owner->guard);
    req_owner->guard = vlc_metadata_pipeline_guard_New();
    if (req_owner->guard == NULL) { vlc_preparser_req_Release(req); return VLC_ENOMEM; }
    PreparserRequestRetain(req);
    vlc_mutex_lock(&preparser->lock);
    const bool accepted = !preparser->closing;
    if (accepted) {
        vlc_list_append(&req_owner->node, &preparser->submitted_tasks); req_owner->listed = true;
    }
    vlc_mutex_unlock(&preparser->lock);
    const int status = accepted ? vlc_metadata_pipeline_PushRequest(preparser->pipeline,
        req_owner->item, req_owner->options, req_owner->guard, req, OnRequestEnded, req) : -EINTR;
    if (status != VLC_SUCCESS) {
        PreparserRemoveTask(preparser, req); vlc_preparser_req_Release(req);
    }
    vlc_preparser_req_Release(req); return status;
}

static size_t preparser_Cancel(void *opaque, vlc_preparser_req *match)
{
    struct preparser_sys *preparser = opaque; size_t count = 0;
    for (;;) {
        struct vlc_preparser_req_owner *request = NULL, *at;
        vlc_mutex_lock(&preparser->lock);
        vlc_list_foreach(at, &preparser->submitted_tasks, node) {
            if (!at->cancel_requested && (match == NULL || at == preparser_req_get_owner(match))) {
                request = at; request->cancel_requested = true; PreparserRequestRetain(&request->req); break;
            }
        }
        vlc_mutex_unlock(&preparser->lock);
        if (request == NULL) break;
        ++count;
        if (request->guard != NULL) {
            if (vlc_metadata_pipeline_guard_TryCancel(request->guard)) {
                Interrupt(&request->req); vlc_metadata_pipeline_Cancel(preparser->pipeline, &request->req);
            }
        } else {
            vlc_mutex_lock(&preparser->lock);
            const bool dequeued = request->executor != NULL && vlc_executor_Cancel(request->executor, &request->runnable);
            atomic_store(&request->interrupted, true);
            vlc_mutex_unlock(&preparser->lock);
            Interrupt(&request->req);
            if (dequeued) {
                PreparserRemoveTask(preparser, &request->req);
                if (request->options & VLC_PREPARSER_TYPE_THUMBNAIL)
                    request->cbs.thumbnailer->on_ended(&request->req, -EINTR, NULL, request->userdata);
                else request->cbs.thumbnailer_to_files->on_ended(&request->req, -EINTR, NULL, 0, request->userdata);
                vlc_preparser_req_Release(&request->req);
            }
        }
        vlc_preparser_req_Release(&request->req);
        if (match != NULL) break;
    }
    return count;
}

static void preparser_Delete(void *opaque)
{
    assert(opaque != NULL);
    struct preparser_sys *preparser = opaque;

    vlc_mutex_lock(&preparser->lock); preparser->closing = true; vlc_mutex_unlock(&preparser->lock);
    vlc_metadata_pipeline_Close(preparser->pipeline);
    /* In case vlc_preparser_Deactivate() has not been called */
    preparser_Cancel(preparser, NULL);

    if (preparser->parser != NULL)
        vlc_executor_Delete(preparser->parser);

    if( preparser->fetcher )
        input_fetcher_Delete( preparser->fetcher );

    if (preparser->thumbnailer != NULL)
        vlc_executor_Delete(preparser->thumbnailer);

    if (preparser->thumbnailer_to_files != NULL)
        vlc_executor_Delete(preparser->thumbnailer_to_files);

    vlc_metadata_pipeline_Delete(preparser->pipeline);
    PreparserRelease(preparser);
}

void*
vlc_preparser_internal_NewWithEngine(vlc_preparser_t *owner, vlc_object_t *parent,
    const struct vlc_preparser_cfg *cfg, const struct vlc_preparser_parse_engine *engine)
{
    assert(cfg->timeout >= 0);

    int request_type = cfg->types;
    assert(request_type & (VLC_PREPARSER_TYPE_FETCHMETA_ALL|
                           VLC_PREPARSER_TYPE_PARSE|
                           VLC_PREPARSER_TYPE_THUMBNAIL|
                           VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES));

    unsigned parser_threads = cfg->max_parser_threads == 0 ? 1 :
                              cfg->max_parser_threads;
    unsigned thumbnailer_threads = cfg->max_thumbnailer_threads == 0 ? 1 :
                                   cfg->max_thumbnailer_threads;

    struct preparser_sys* preparser = malloc( sizeof *preparser );
    if (!preparser)
        return NULL;

    vlc_atomic_rc_init(&preparser->refs); preparser->closing = false; preparser->pipeline = NULL;
    preparser->engine = engine != NULL ? *engine : (struct vlc_preparser_parse_engine){0};
    preparser->timeout = cfg->timeout;
    preparser->owner = parent;

    if (request_type & (VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL))
    {
        preparser->parser = vlc_executor_New(parser_threads);
        if (!preparser->parser)
            goto error_parser;
    }
    else
        preparser->parser = NULL;

    if (request_type & VLC_PREPARSER_TYPE_FETCHMETA_ALL)
    {
        preparser->fetcher = input_fetcher_New(parent, request_type | VLC_PREPARSER_TYPE_FETCHMETA_LOCAL);
        if (unlikely(preparser->fetcher == NULL))
            goto error_fetcher;
    }
    else
        preparser->fetcher = NULL;

    if (request_type & (VLC_PREPARSER_TYPE_THUMBNAIL |
                        VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES))
    {
        preparser->thumbnailer = vlc_executor_New(thumbnailer_threads);
        if (!preparser->thumbnailer)
            goto error_thumbnail;
    }
    else
        preparser->thumbnailer = NULL;

    if (request_type & VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES)
    {
        preparser->thumbnailer_to_files = vlc_executor_New(1);
        if (preparser->thumbnailer_to_files == NULL)
            goto error_thumbnail_to_files;
    }
    else
        preparser->thumbnailer_to_files = NULL;

    vlc_mutex_init(&preparser->lock);
    vlc_list_init(&preparser->submitted_tasks);
    static const struct vlc_metadata_pipeline_operations pipeline_ops = {.local=StartNativeStage, .cancel_local=CancelNativeStage,
        .retain_key=RetainRequestKey, .release_key=ReleaseRequestKey};
    preparser->pipeline = vlc_metadata_pipeline_New(parent, &pipeline_ops, preparser);
    if (preparser->pipeline == NULL) goto error_pipeline;

    static const struct vlc_preparser_operations ops = {
        .req_new_parse = preparser_req_NewParse,
        .req_new_thumbnail = preparser_req_NewThumbnail,
        .req_new_thumbnail_to_files = preparser_req_NewThumbnailToFiles,
        .submit = preparser_Submit,
        .cancel = preparser_Cancel,
        .delete = preparser_Delete,
    };
    assert(owner != NULL);
    owner->ops = &ops;

    return preparser;

error_pipeline:
    if (preparser->thumbnailer_to_files != NULL) vlc_executor_Delete(preparser->thumbnailer_to_files);
error_thumbnail_to_files:
    if (preparser->thumbnailer != NULL)
        vlc_executor_Delete(preparser->thumbnailer);
error_thumbnail:
    if (preparser->fetcher != NULL)
        input_fetcher_Delete(preparser->fetcher);
error_fetcher:
    if (preparser->parser != NULL)
        vlc_executor_Delete(preparser->parser);
error_parser:
    free(preparser);
    return NULL;
}

void *vlc_preparser_internal_New(vlc_preparser_t *owner, vlc_object_t *parent,
    const struct vlc_preparser_cfg *cfg)
{ return vlc_preparser_internal_NewWithEngine(owner, parent, cfg, NULL); }
