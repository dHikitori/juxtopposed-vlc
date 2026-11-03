// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * external.c: preparser with external process
 *****************************************************************************
 * Copyright © 2025 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_POLL_H
# include <poll.h>
#endif

#include <assert.h>

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_input_item.h>
#include <vlc_preparser.h>
#include <vlc_preparser_ipc.h>
#include <vlc_spawn.h>
#include <vlc_threads.h>
#include <vlc_interrupt.h>
#include <vlc_list.h>
#include <vlc_memstream.h>
#include <vlc_vector.h>
#include <vlc_arrays.h>
#include <vlc_executor.h>
#include <vlc_tick.h>
#include <vlc_process.h>

#include "preparser.h"
#include "input/item_parse.h"
#include <vlc_input_policy.h>
#include <stdatomic.h>

#define VLC_PREPARSER_PATH "vlc-preparser"

/*****************************************************************************
 * Preparser serdes callbacks functions
 *****************************************************************************/

struct preparser_serdes_cbs_ctx {
    vlc_tick_t timeout;
    vlc_tick_t start;
    struct vlc_process *process;
    size_t bytes, maximum_bytes;
};

static ssize_t
write_cbs(const void *data, size_t size, void *userdata)
{
    struct preparser_serdes_cbs_ctx *ctx = userdata;
    assert(ctx->start != VLC_TICK_INVALID);
    assert(ctx->process != NULL);

    int timeout_ms = -1;
    if (ctx->timeout != VLC_TICK_INVALID) {
        vlc_tick_t time_elapsed = (vlc_tick_now() - ctx->start);
        if (time_elapsed >= ctx->timeout) {
            errno = ETIMEDOUT;
            return -1;
        }
        timeout_ms = MS_FROM_VLC_TICK(ctx->timeout - time_elapsed);
    }

    int ret = vlc_process_fd_Write(ctx->process, data, size, timeout_ms);
    return ret;
}

static ssize_t
read_cbs(void *data, size_t size, void *userdata)
{
    struct preparser_serdes_cbs_ctx *ctx = userdata;
    assert(ctx->start != VLC_TICK_INVALID);
    assert(ctx->process != NULL);

    int timeout_ms = -1;
    if (ctx->timeout != VLC_TICK_INVALID) {
        vlc_tick_t time_elapsed = (vlc_tick_now() - ctx->start);
        if (time_elapsed >= ctx->timeout) {
            errno = ETIMEDOUT;
            return -1;
        }
        timeout_ms = MS_FROM_VLC_TICK(ctx->timeout - time_elapsed);
    }

    if (ctx->bytes >= ctx->maximum_bytes) { errno = EFBIG; return -1; }
    size = __MIN(size, ctx->maximum_bytes - ctx->bytes);
    int ret = vlc_process_fd_Read(ctx->process, data, size, timeout_ms);
    if (ret > 0) ctx->bytes += ret;
    return ret;
}

/*****************************************************************************
 * preparser_sys structure
 *****************************************************************************/

struct preparser_sys {
    vlc_object_t *parent;

    /* shared data */
    atomic_size_t current_id; 

    /* process pool for the preparser */
    struct preparser_process_pool *pool_preparser;

    /* process pool for the thumbnailer */
    struct preparser_process_pool *pool_thumbnailer;
    struct vlc_preparser_t requests;
    bool has_requests;
};

/*****************************************************************************
 * Preparser process thread structure
 *****************************************************************************/

struct preparser_process_thread {
    /* Node of the process_pool.thread list */
    struct vlc_list node;

    /* The system thread */
    vlc_thread_t thread;

    /* The process_pool owning the thread */
    struct preparser_process_pool *owner;

    /* The current process running */
    struct vlc_process *process;
    bool process_running;

    /* Status of the current thread */
    bool stopped;

    /* The current task */
    struct preparser_task *task;

    /* The thread serializer */
    struct vlc_preparser_msg_serdes *serdes;
};

/*****************************************************************************
 * Task functions
 *****************************************************************************/


union preparser_task_cbs
{
    const struct vlc_preparser_cbs *parser;
    const struct vlc_thumbnailer_cbs *thumbnailer;
    const struct vlc_thumbnailer_to_files_cbs *thumbnailer_to_files;
};

struct preparser_task {
    /** Request message */
    struct vlc_preparser_msg req_msg;

    /** Response message */
    struct vlc_preparser_msg res_msg;

    /** The preparser request */
    struct vlc_preparser_req req;
    vlc_atomic_rc_t rc;

    /** Input item used for the request */
    input_item_t *item;

    /* Thread interrupt */
    vlc_interrupt_t *interrupt;

    /** Preparser callbacks */
    union preparser_task_cbs cbs;
    void *cbs_userdata;

    struct preparser_sys *owner;
    vlc_tick_t deadline;
    atomic_bool cancelled, cancel_issued, callback_done;

    struct vlc_list node; /**< node of vlc_preparser_t.submitted_tasks */
};

static struct preparser_task *
preparser_task_get_req_owner(struct vlc_preparser_req *req)
{
    return container_of(req, struct preparser_task, req);
}

/**
 * Execute task callbacks
 */
static int import_native_result(input_item_t *target, input_item_t *source)
{
    if (source == NULL || source->psz_uri == NULL || target->psz_uri == NULL
        || strcmp(target->psz_uri, source->psz_uri)) return VLC_EINVAL;
    vlc_mutex_lock(&source->lock);
    input_item_t *copy = vlc_item_parse_CopyLocked(source);
    vlc_mutex_unlock(&source->lock);
    if (copy == NULL) return VLC_ENOMEM;
    struct vlc_item_parse_delta delta = {.item=copy,
        .fields=VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_DURATION) | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_TRACKS)
            | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_INFOS) | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_EPG)
            | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_TYPE) | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_SLAVES)};
    vlc_mutex_lock(&target->lock);
    int status = target->psz_uri != NULL && !strcmp(target->psz_uri, copy->psz_uri)
        ? vlc_item_parse_PrepareLinksLocked(&delta, target) : VLC_EINVAL;
    if (status == VLC_SUCCESS) {
        if (target->psz_name == NULL) delta.fields |= VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_NAME);
        /* All allocation precedes these private-item writes. The final native
         * target admission still occurs in the internal request owner. */
        vlc_meta_t *previous = target->p_meta;
        target->p_meta = copy->p_meta; copy->p_meta = previous;
        vlc_item_parse_SwapLocked(&delta, target, delta.fields);
    }
    vlc_mutex_unlock(&target->lock);
    vlc_item_parse_Clear(&delta); return status;
}

static void
preparser_task_ExecCallback(struct preparser_task *task, int status)
{
    assert(task != NULL);
    assert(task->item != NULL);
    assert(task->req_msg.type == VLC_PREPARSER_MSG_TYPE_REQ);
    if (atomic_exchange(&task->callback_done, true)) return;
    struct vlc_preparser_msg *res_msg = &task->res_msg;
    if (atomic_load(&task->cancelled)) status = -EINTR;
    if (status == VLC_SUCCESS && task->req_msg.req_type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE)
        status = import_native_result(task->item, res_msg->res.item);
    if (status != 0) {
        vlc_preparser_msg_Clean(res_msg);
        vlc_preparser_msg_Init(res_msg, VLC_PREPARSER_MSG_TYPE_RES,
                               task->req_msg.req_type);
        res_msg->res.item = input_item_Hold(task->item);
        res_msg->res.status = status;
    } else {
        assert(task->req_msg.req_type == task->res_msg.req_type);
        if (task->req_msg.req_type != VLC_PREPARSER_MSG_REQ_TYPE_PARSE)
            input_item_Update(task->item, res_msg->res.item);
    }

    switch (task->req_msg.req_type) {
        case VLC_PREPARSER_MSG_REQ_TYPE_PARSE:
            assert(task->cbs.parser != NULL);
            if (task->cbs.parser->on_subtree_added != NULL
                    && res_msg->res.subtree != NULL) {
                /* IPC creates a new root item. Keep the original identity so
                 * listeners can find the media in the queue or media tree. */
                input_item_t *root = input_item_Hold(task->item);
                input_item_Release(res_msg->res.subtree->p_item);
                res_msg->res.subtree->p_item = root;
                task->cbs.parser->on_subtree_added(&task->req,
                                                   res_msg->res.subtree,
                                                   task->cbs_userdata);
                res_msg->res.subtree = NULL;
            }
            if (task->cbs.parser->on_subtree_added != NULL) {
                for (size_t i = 0; i < res_msg->res.subtrees.size; ++i) {
                    input_item_node_t *tree = res_msg->res.subtrees.data[i];
                    input_item_Release(tree->p_item); tree->p_item = input_item_Hold(task->item);
                    task->cbs.parser->on_subtree_added(&task->req, tree, task->cbs_userdata);
                    res_msg->res.subtrees.data[i] = NULL;
                }
                vlc_vector_clear(&res_msg->res.subtrees);
            }
            if (task->cbs.parser->on_attachments_added != NULL
                    && res_msg->res.attachments.size != 0) {
                task->cbs.parser->on_attachments_added(&task->req,
                                        res_msg->res.attachments.data,
                                        res_msg->res.attachments.size,
                                        task->cbs_userdata);
            }
            assert(task->cbs.parser->on_ended != NULL);
            task->cbs.parser->on_ended(&task->req, res_msg->res.status,
                                       task->cbs_userdata);
            break;
        case VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL:
            assert(task->cbs.thumbnailer->on_ended != NULL);
            task->cbs.thumbnailer->on_ended(&task->req,
                                            res_msg->res.status,
                                            res_msg->res.pic,
                                            task->cbs_userdata);
            break;
        case VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL_TO_FILES:
            assert(task->cbs.thumbnailer_to_files->on_ended != NULL);
            task->cbs.thumbnailer_to_files->on_ended(&task->req,
                                                res_msg->res.status,
                                                res_msg->res.result.data,
                                                res_msg->res.result.size,
                                                task->cbs_userdata);
            break;
        default:
            vlc_assert_unreachable();
    }
}

/**
 * Execute a task: Send a request and wait for a response then call callbacks.
 */
static int
preparser_task_Run(struct preparser_process_thread *thread, vlc_tick_t timeout)
{
    vlc_thread_set_name("vlc-task-run");

    assert(thread != NULL);
    assert(thread->process != NULL);
    assert(thread->task != NULL);

    struct preparser_task *task = thread->task;
    assert(task->item != NULL);

    struct preparser_serdes_cbs_ctx serdes_ctx = {
        .start = vlc_tick_now(),
        .timeout = timeout > 0 ? timeout : VLC_TICK_INVALID,
        .process = thread->process,
        .maximum_bytes = task->req_msg.req_type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE
            ? 256u * 1024u * 1024u : SIZE_MAX,
    };
    if (task->deadline != VLC_TICK_INVALID) {
        const vlc_tick_t remaining = task->deadline - serdes_ctx.start;
        if (remaining <= 0) return VLC_ETIMEOUT;
        if (serdes_ctx.timeout == VLC_TICK_INVALID || remaining < serdes_ctx.timeout) serdes_ctx.timeout = remaining;
    }

    int status = VLC_SUCCESS;
    int ret = vlc_preparser_msg_serdes_Serialize(thread->serdes,
                                                 &task->req_msg, &serdes_ctx);
    if (ret != VLC_SUCCESS) {
        status = ret;
        goto end;
    }

    ret = vlc_preparser_msg_serdes_Deserialize(thread->serdes, &task->res_msg,
                                               &serdes_ctx);
    if (ret != VLC_SUCCESS) status = ret;
    else if (task->res_msg.type != VLC_PREPARSER_MSG_TYPE_RES
        || task->res_msg.req_type != task->req_msg.req_type || task->res_msg.res.type != task->req_msg.req_type)
        status = VLC_EINVAL;

end:
    return status;
}

static struct vlc_preparser_req *
preparser_task_req_Hold(struct vlc_preparser_req *req)
{
    assert(req != NULL);
    struct preparser_task *task = preparser_task_get_req_owner(req);
    vlc_atomic_rc_inc(&task->rc);
    return req;
}

static input_item_t *
preparser_task_req_GetItem(struct vlc_preparser_req *req)
{
    assert(req != NULL);
    struct preparser_task *task = preparser_task_get_req_owner(req);
    return task->item;
}

static void
preparser_task_req_Release(struct vlc_preparser_req *req)
{
    assert(req != NULL);
    struct preparser_task *task = preparser_task_get_req_owner(req);
    assert(task->item != NULL);

    if (!vlc_atomic_rc_dec(&task->rc)) {
        return;
    }

    vlc_interrupt_destroy(task->interrupt);
    vlc_preparser_msg_Clean(&task->req_msg);
    vlc_preparser_msg_Clean(&task->res_msg);
    input_item_Release(task->item);

    free(task);
}

/**
 * Create a new task.
 */
static bool
preparser_req_AddInputOption(struct vlc_preparser_msg_req *req,
                            const char *value, unsigned flags)
{
    struct vlc_preparser_input_option option = {
        .value = strdup(value),
        .flags = flags,
    };
    if (option.value == NULL)
        return false;
    if (!vlc_vector_push(&req->input_options, option)) {
        free(option.value);
        return false;
    }
    return true;
}

static struct preparser_task*
preparser_task_New(vlc_object_t *parent, input_item_t *item,
                   enum vlc_preparser_msg_req_type req_type)
{
    assert(item != NULL);
    assert(req_type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE ||
           req_type == VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL ||
           req_type == VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL_TO_FILES);

    struct preparser_task *task = malloc(sizeof(*task));
    if (task == NULL) {
        return NULL;
    }
    vlc_preparser_msg_Init(&task->req_msg, VLC_PREPARSER_MSG_TYPE_REQ,
                           req_type);

    vlc_preparser_msg_Init(&task->res_msg, VLC_PREPARSER_MSG_TYPE_RES,
                           req_type);

    task->interrupt = vlc_interrupt_create();
    if (task->interrupt == NULL) {
        vlc_preparser_msg_Clean(&task->req_msg);
        vlc_preparser_msg_Clean(&task->res_msg);
        free(task);
        return NULL;
    }

    if (item->psz_uri != NULL) {
        task->req_msg.req.uri = strdup(item->psz_uri);
        if (task->req_msg.req.uri == NULL) {
            vlc_interrupt_destroy(task->interrupt);
            vlc_preparser_msg_Clean(&task->req_msg);
            vlc_preparser_msg_Clean(&task->res_msg);
            free(task);
            return NULL;
        }
    }

    /* Workers have independent configuration. Snapshot the current folder
     * defaults for each request, then apply media-specific options last. */
    const char *hidden = var_InheritBool(parent, "show-hiddenfiles")
                       ? "show-hiddenfiles" : "no-show-hiddenfiles";
    char *ignored = var_InheritString(parent, "ignore-filetypes");
    char *filter = NULL;
    bool copied = asprintf(&filter, "ignore-filetypes=%s",
                           ignored != NULL ? ignored : "") >= 0;
    free(ignored);
    if (copied)
        copied = preparser_req_AddInputOption(&task->req_msg.req, hidden,
                                              VLC_INPUT_OPTION_TRUSTED)
              && preparser_req_AddInputOption(&task->req_msg.req, filter,
                                              VLC_INPUT_OPTION_TRUSTED);
    free(filter);

    vlc_mutex_lock(&item->lock);
    for (int i = 0; copied && i < item->i_options; ++i)
        copied = preparser_req_AddInputOption(&task->req_msg.req,
                                              item->ppsz_options[i],
                                              item->optflagv[i]);
    vlc_mutex_unlock(&item->lock);
    if (!copied) {
        vlc_interrupt_destroy(task->interrupt);
        vlc_preparser_msg_Clean(&task->req_msg);
        vlc_preparser_msg_Clean(&task->res_msg);
        free(task);
        return NULL;
    }

    task->item = input_item_Hold(item);
    task->deadline = VLC_TICK_INVALID;
    task->owner = NULL;
    atomic_init(&task->cancelled, false); atomic_init(&task->cancel_issued, false); atomic_init(&task->callback_done, false);
    task->cbs_userdata = NULL;
    vlc_list_init(&task->node);

    static const struct vlc_preparser_req_operations ops = {
        .get_item = preparser_task_req_GetItem,
        .release = preparser_task_req_Release,
    };
    task->req.ops = &ops;
    task->req.submitted = false;
    vlc_atomic_rc_init(&task->rc);

    return task;
}

/**
 * Init a task for a Push request.
 */
static void
preparser_task_InitPush(struct preparser_task *task, int options,
                        const union preparser_task_cbs *cbs, void *userdata)
{
    assert(task != NULL);
    struct vlc_preparser_msg *msg = &task->req_msg;
    assert(msg != NULL);
    assert(msg->type == VLC_PREPARSER_MSG_TYPE_REQ);
    assert(msg->req_type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
    assert(cbs != NULL);

    msg->req.options = options;

    task->cbs = *cbs;
    task->cbs_userdata = userdata;
}

/**
 * Init a task for a Generate Thumbnail request.
 */
static void
preparser_task_InitThumbnail(struct preparser_task *task,
                             const struct vlc_thumbnailer_arg *arg,
                             const union preparser_task_cbs *cbs,
                             void *userdata)
{
    assert(task != NULL);
    struct vlc_preparser_msg *msg = &task->req_msg;
    assert(msg != NULL);
    assert(msg->type == VLC_PREPARSER_MSG_TYPE_REQ);
    assert(msg->req_type == VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL);
    assert(cbs != NULL);

    if (arg == NULL) {
        msg->req.arg.seek.type = VLC_THUMBNAILER_SEEK_NONE;
        msg->req.arg.hw_dec = false;
    } else {
        msg->req.arg = *arg;
    }

    task->cbs = *cbs;
    task->cbs_userdata = userdata;
}

/**
 * Init a task for a Generate Thumbnail To Files request.
 */
static void
preparser_task_InitThumbnailToFile(struct preparser_task *task,
                                const struct vlc_thumbnailer_arg *arg,
                                const struct vlc_thumbnailer_output *outputs,
                                size_t output_count,
                                const union preparser_task_cbs *cbs,
                                void *userdata)
{
    assert(task != NULL);
    struct vlc_preparser_msg *msg = &task->req_msg;
    assert(msg != NULL);
    assert(msg->type == VLC_PREPARSER_MSG_TYPE_REQ);
    assert(msg->req_type == VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL_TO_FILES);
    assert(output_count == 0 || outputs != NULL);
    assert(cbs != NULL);

    if (arg == NULL) {
        msg->req.arg.seek.type = VLC_THUMBNAILER_SEEK_NONE;
        msg->req.arg.hw_dec = false;
    } else {
        msg->req.arg = *arg;
    }
    if (output_count != 0) {
        vlc_vector_push_all(&msg->req.outputs, outputs, output_count);
    }

    task->cbs = *cbs;
    task->cbs_userdata = userdata;
}

/*****************************************************************************
 * Process Pool functions
 *****************************************************************************/

struct preparser_process_pool {
    vlc_mutex_t lock;

    /** Maximum number of threads to run the tasks */
    unsigned max_threads;

    /** List of active preparser_process_thread */
    struct vlc_list threads;

    /** Number of running thread */
    size_t nthreads;

    /** Number of stopped thread */
    size_t nthreads_stopped;

    /** Unfinished task */
    size_t unfinished;

    /** List of running preparser_task */
    struct vlc_list running;

    /** Queue of preparser_task */
    struct vlc_list queue;

    /** Wait for the queue to be non-empty */
    vlc_cond_t queue_wait;

    /** True if pool deletion is requested */
    bool closing;

    /** Preparser process arguments */
    vlc_tick_t timeout;
    int types;

    char **argv;
    int argc;

    /** Parent object */
    vlc_object_t *parent;
};

/**
 * Add a task to the process pool queue and trigger the `queue_wait`.
 */
static void
preparser_pool_QueuePush(struct preparser_process_pool *pool,
                         struct preparser_task *task)
{
    assert(pool != NULL);
    vlc_mutex_assert(&pool->lock);
    assert(task != NULL);

    vlc_list_append(&task->node, &pool->queue);
    vlc_cond_signal(&pool->queue_wait);
}

/**
 * Take a task on the queue or wait for a new one to be added.
 */
static struct preparser_task*
preparser_pool_QueueTake(struct preparser_process_pool *pool)
{
    assert(pool != NULL);
    vlc_mutex_assert(&pool->lock);

    while (!pool->closing && vlc_list_is_empty(&pool->queue)) {
        vlc_cond_wait(&pool->queue_wait, &pool->lock);
    }

    if (pool->closing) {
        return NULL;
    }

    struct preparser_task *task = NULL;
    task = vlc_list_first_entry_or_null(&pool->queue, struct preparser_task,
                                        node);
    assert(task != NULL);
    vlc_list_remove(&task->node);

    return task;
}

static int
preparser_pool_SpawnProcess(struct preparser_process_thread *thread)
{
    assert(thread != NULL);
    vlc_mutex_assert(&thread->owner->lock);
    struct preparser_process_pool *pool = thread->owner;

    char *str_timeout = NULL;
    if (asprintf(&str_timeout, "%" PRId64, pool->timeout) < 0) {
        return VLC_ENOMEM;
    }
    char *str_types = NULL;
    if (asprintf(&str_types, "%" PRId32, pool->types) < 0) {
        free(str_timeout);
        return VLC_ENOMEM;
    }

    const char *argv[] = {
        "--timeout-tick",
        str_timeout,
        "--types",
        str_types,
        "--daemon",
        NULL,
    };
    int argc = ARRAY_SIZE(argv);

    char *path = NULL;
#ifdef _WIN32
    const char *name = VLC_PREPARSER_PATH ".exe";
    const char *static_name = VLC_PREPARSER_PATH "-static.exe";
#else
    const char *name = VLC_PREPARSER_PATH;
    const char *static_name = VLC_PREPARSER_PATH "-static";
#endif

    /* When running from build directory (vlc-static), VLC_LIB_PATH points to
     * TOP_BUILDDIR/modules, so we need to construct TOP_BUILDDIR/bin to
     * start vlc-preparser-static */
    const char *lib_path = getenv("VLC_LIB_PATH");
    if (lib_path != NULL) {
        size_t len = strlen(lib_path);
        if (len > 8 && strcmp(lib_path + len - 8, "/modules") == 0) {
            if (asprintf(&path, "%.*s/bin/%s", (int)(len - 8),
                         lib_path, static_name) < 0) {
                free(str_timeout);
                free(str_types);
                return VLC_ENOMEM;
            }
        }
    }

    /* If not in build directory, use the standard installed location */
    if (path == NULL) {
        path = config_GetSysPath(VLC_PKG_LIBEXEC_DIR, name);
    }

    if (path == NULL) {
        thread->process = vlc_process_Spawn(name, argc, argv);
    } else {
        thread->process = vlc_process_Spawn(path, argc, argv);
    }
    free(path);
    free(str_timeout);
    free(str_types);

    if (thread->process == NULL) {
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

/**
 * Process thread loop.
 * Take a task on the queue, then execute the task and delete it.
 * Check that the external process has not crashed otherwise start a new one.
 */
static void*
preparser_pool_Run(void *data)
{
    struct preparser_process_thread *thread = data;
    assert(thread != NULL);
    assert(thread->owner != NULL);
    assert(thread->process != NULL);

    vlc_thread_set_name("vlc-pool-runner");


    vlc_mutex_lock(&thread->owner->lock);
    struct preparser_task *task = NULL;
    while ((task = preparser_pool_QueueTake(thread->owner))) {
        thread->task = task;
        vlc_list_append(&task->node, &thread->owner->running);

        vlc_tick_t timeout = thread->owner->timeout;
        vlc_interrupt_t *old = vlc_interrupt_set(task->interrupt);

        vlc_mutex_unlock(&thread->owner->lock);
        int status = VLC_SUCCESS;
        if (thread->process == NULL) {
            vlc_mutex_lock(&thread->owner->lock);
            status = preparser_pool_SpawnProcess(thread);
            if (status == VLC_SUCCESS) thread->process_running = true;
            vlc_mutex_unlock(&thread->owner->lock);
        }
        if (status == VLC_SUCCESS) status = preparser_task_Run(thread, timeout);
        /* Reap each Parse child before the parent fetch stage. Keep successful
         * thumbnail transport for reuse. Detach under the pool lock. */
        if (status != VLC_SUCCESS || task->req_msg.req_type == VLC_PREPARSER_MSG_REQ_TYPE_PARSE) {
            vlc_mutex_lock(&thread->owner->lock);
            struct vlc_process *process = thread->process;
            thread->process = NULL; thread->process_running = false;
            vlc_mutex_unlock(&thread->owner->lock);
            if (process != NULL) vlc_process_Terminate(process, true);
        }
        vlc_interrupt_set(old);
        preparser_task_ExecCallback(task, status);
        vlc_mutex_lock(&thread->owner->lock);
        vlc_list_remove(&task->node); thread->task = NULL;
        assert(thread->owner->unfinished > 0); --thread->owner->unfinished;
        vlc_mutex_unlock(&thread->owner->lock);
        preparser_task_req_Release(&task->req);
        vlc_mutex_lock(&thread->owner->lock);
        if (thread->owner->closing) break;
    }
    thread->owner->nthreads--;
    thread->owner->nthreads_stopped++;
    thread->stopped = true;
    vlc_mutex_unlock(&thread->owner->lock);
    return NULL;
}

/**
 * Start a new thread process with its external process.
 */
static int
preparser_pool_SpawnThreadLocked(struct preparser_process_pool *pool)
{
    assert(pool != NULL);
    vlc_mutex_assert(&pool->lock);
    assert(pool->nthreads < pool->max_threads);

    struct preparser_process_thread *thread = malloc(sizeof(*thread));
    if (thread == NULL) {
        return -ENOMEM;
    }

    thread->owner = pool;
    thread->task = NULL;
    thread->stopped = false;

    static struct vlc_preparser_msg_serdes_cbs cbs = {
        .write = write_cbs,
        .read = read_cbs,
    };
    thread->serdes = vlc_preparser_msg_serdes_Create(pool->parent, &cbs, true);
    if (thread->serdes == NULL) {
        free(thread);
        return VLC_EGENERIC;
    }

    if (preparser_pool_SpawnProcess(thread) != VLC_SUCCESS) {
        msg_Err(pool->parent, "Fail to create Process in process_pool");
        vlc_preparser_msg_serdes_Delete(thread->serdes);
        free(thread);
        return VLC_EGENERIC;
    }
    thread->process_running = true;

    int ret = vlc_clone(&thread->thread, preparser_pool_Run, thread);
    if (ret != 0) {
        vlc_process_Terminate(thread->process, false);
        vlc_preparser_msg_serdes_Delete(thread->serdes);
        free(thread);
        return VLC_EGENERIC;
    }
    pool->nthreads++;
    vlc_list_append(&thread->node, &pool->threads);

    return VLC_SUCCESS;
}

/**
 * Join and free all stopped threads in the pool.
 * Must be called with pool->lock held. Collects stopped threads under the lock,
 * then releases it once to join them all, and re-acquires it before returning.
 */
static void
preparser_pool_JoinStoppedThreadsLocked(struct preparser_process_pool *pool)
{
    vlc_mutex_assert(&pool->lock);

    assert(pool->nthreads_stopped != 0);

    /* Move all stopped threads to a local list while holding the lock */
    struct vlc_list to_join;
    vlc_list_init(&to_join);

    struct preparser_process_thread *t;
    vlc_list_foreach(t, &pool->threads, node) {
        if (t->stopped) {
            vlc_list_remove(&t->node);
            vlc_list_append(&t->node, &to_join);
            pool->nthreads_stopped--;
        }
    }

    vlc_mutex_unlock(&pool->lock);
    vlc_list_foreach(t, &to_join, node) {
        vlc_join(t->thread, NULL);
        if (t->process != NULL)
            vlc_process_Terminate(t->process, false);
        vlc_preparser_msg_serdes_Delete(t->serdes);
        free(t);
    }
    vlc_mutex_lock(&pool->lock);
}

/**
 * Push a new task in the queue and check if a new process thread can be spawn.
 * If there is more unfinished task than spawned process thread and that the
 * max number of process thread is not reached, then a new thread is spwaned.
 * Return VLC_SUCCESS, or VLC_EGENERIC if no thread is available and a new one could
 * not be spawned. On failure nothing takes ownership of the task, the caller
 * keeps its reference and no callback is fired.
 */
static int
preparser_pool_Submit(struct preparser_process_pool *pool,
                      struct preparser_task *task)
{
    assert(pool != NULL);
    assert(task != NULL);

    vlc_input_policy_t *policy = vlc_input_policy_HoldItem(task->item);
    const bool scoped = policy != NULL || vlc_input_policy_Has(pool->parent)
        || vlc_input_policy_IsAddonSourceURI(task->req_msg.req.uri);
    vlc_input_policy_Release(policy);
    if (scoped) return -EACCES;
    vlc_mutex_lock(&pool->lock);
    if (pool->closing || atomic_load(&task->cancelled)) {
        vlc_mutex_unlock(&pool->lock); return -EINTR;
    }
    if (task->deadline != VLC_TICK_INVALID && vlc_tick_now() >= task->deadline) {
        vlc_mutex_unlock(&pool->lock); return VLC_ETIMEOUT;
    }

    if (pool->nthreads_stopped != 0) {
        preparser_pool_JoinStoppedThreadsLocked(pool);
    }

    if (pool->nthreads == 0) {
        if (preparser_pool_SpawnThreadLocked(pool) != VLC_SUCCESS) {
            vlc_mutex_unlock(&pool->lock);
            return VLC_EGENERIC;
        }
    }

    preparser_task_req_Hold(&task->req);

    preparser_pool_QueuePush(pool, task);
    ++pool->unfinished;

    /* Opportunistically spawn an extra thread if tasks outnumber threads */
    if (pool->unfinished > pool->nthreads &&
        pool->nthreads < pool->max_threads)
        /* If it fails, this is not an error, there is at least one thread */
        preparser_pool_SpawnThreadLocked(pool);

    vlc_mutex_unlock(&pool->lock);
    return VLC_SUCCESS;
}

/**
 * Cancel a request. If `NULL` is given, all request are canceled.
 */
static size_t
preparser_pool_Cancel(struct preparser_process_pool *pool, struct vlc_preparser_req *match)
{
    size_t count = 0;
    for (;;) {
        struct preparser_task *selected = NULL, *task; bool queued = false;
        vlc_mutex_lock(&pool->lock);
        vlc_list_foreach(task, &pool->queue, node) {
            if ((match == NULL || match == &task->req) && !atomic_exchange(&task->cancel_issued, true)) {
                selected = task; queued = true; vlc_list_remove(&task->node); --pool->unfinished; break;
            }
        }
        if (selected == NULL) vlc_list_foreach(task, &pool->running, node) {
            if ((match == NULL || match == &task->req) && !atomic_exchange(&task->cancel_issued, true)) {
                selected = task; break;
            }
        }
        if (selected != NULL) {
            atomic_store(&selected->cancelled, true); preparser_task_req_Hold(&selected->req);
        }
        vlc_mutex_unlock(&pool->lock);
        if (selected == NULL) break;
        ++count;
        vlc_interrupt_kill(selected->interrupt);
        if (queued) {
            preparser_task_ExecCallback(selected, -EINTR);
            preparser_task_req_Release(&selected->req); /* queue reference */
        }
        preparser_task_req_Release(&selected->req);
        if (match != NULL) break;
    }
    return count;
}

/**
 * Clear and delete the process pool.
 */
static void
preparser_pool_Delete(struct preparser_process_pool *pool)
{
    assert(pool != NULL);

    vlc_mutex_lock(&pool->lock);

    /* All the tasks must be canceled on delete */
    assert(vlc_list_is_empty(&pool->queue));

    pool->closing = true;
    /* "closing" is now true, this will wake up threads */
    vlc_cond_broadcast(&pool->queue_wait);

    struct preparser_process_thread *thread = NULL;
    vlc_list_foreach(thread, &pool->threads, node) {
        if (thread->process != NULL) {
            vlc_process_Kill(thread->process);
        }
    }

    vlc_mutex_unlock(&pool->lock);

    /* The threads list may not be written at this point, so it is safe to read
     * it without mutex locked (the mutex must be released to join the
     * threads). */
    vlc_list_foreach(thread, &pool->threads, node) {
        vlc_join(thread->thread, NULL);
        if (thread->process != NULL) {
            vlc_process_Terminate(thread->process, false);
        }
        vlc_preparser_msg_serdes_Delete(thread->serdes);
        free(thread);
    }

    /* The queue must still be empty (no runnable submitted a new runnable) */
    assert(vlc_list_is_empty(&pool->queue));

    /* There are no tasks anymore */
    assert(!pool->unfinished);

    free(pool);
}

/**
 * Create a new process pool with `max` maximum process.
 */
static struct preparser_process_pool*
preparser_pool_New(vlc_object_t *obj, size_t max, vlc_tick_t timeout,
                   int types)
{
    assert(obj != NULL);
    assert(max != 0);

    struct preparser_process_pool *pool = malloc(sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }

    vlc_mutex_init(&pool->lock);

    pool->max_threads = max;
    pool->nthreads = 0;
    pool->nthreads_stopped = 0;
    pool->unfinished = 0;
    pool->timeout = timeout;
    pool->types = types;

    vlc_list_init(&pool->threads);
    vlc_list_init(&pool->queue);
    vlc_list_init(&pool->running);

    vlc_cond_init(&pool->queue_wait);

    pool->closing = false;
    pool->parent = obj;

    /* Start only after an admitted request passes the native scope gate. */
    return pool;
}



/* The engine holds one reference through interrupt removal. The child callback
 * holds another reference through its final semaphore post. */
struct child_engine {
    vlc_atomic_rc_t refs;
    struct preparser_process_pool *pool;
    struct preparser_task *task;
    struct input_item_parser_cfg cfg;
    vlc_sem_t done;
    int status;
};
static struct child_engine *child_engine_Hold(struct child_engine *engine)
{ vlc_atomic_rc_inc(&engine->refs); return engine; }
static void child_engine_Release(struct child_engine *engine)
{
    if (!vlc_atomic_rc_dec(&engine->refs)) return;
    if (engine->task != NULL) preparser_task_req_Release(&engine->task->req);
    free(engine);
}
static void child_engine_Ended(vlc_preparser_req *request, int status, void *opaque)
{
    (void)request; struct child_engine *engine = opaque;
    engine->status = status; vlc_sem_post(&engine->done); child_engine_Release(engine);
}
static void child_engine_Subtree(vlc_preparser_req *request, input_item_node_t *tree, void *opaque)
{
    struct child_engine *engine = opaque;
    if (engine->cfg.cbs->on_subtree_added != NULL)
        engine->cfg.cbs->on_subtree_added(vlc_preparser_req_GetItem(request), tree, engine->cfg.cbs_data);
    else input_item_node_Delete(tree);
}
static void child_engine_Attachments(vlc_preparser_req *request, input_attachment_t *const *array,
    size_t count, void *opaque)
{
    struct child_engine *engine = opaque;
    if (engine->cfg.cbs->on_attachments_added != NULL)
        engine->cfg.cbs->on_attachments_added(vlc_preparser_req_GetItem(request), array, count, engine->cfg.cbs_data);
}
static void child_engine_Cancel(void *opaque)
{
    struct child_engine *engine = opaque;
    atomic_store(&engine->task->cancelled, true);
    vlc_interrupt_kill(engine->task->interrupt);
    preparser_pool_Cancel(engine->pool, &engine->task->req);
}
static int run_child_engine(void *opaque, input_item_t *item,
    const struct input_item_parser_cfg *cfg, vlc_tick_t deadline, vlc_interrupt_t *interrupt)
{
    struct preparser_sys *sys = opaque;
    vlc_input_policy_t *policy = vlc_input_policy_HoldItem(item);
    const bool scoped = policy != NULL || vlc_input_policy_Has(sys->parent)
        || vlc_input_policy_IsAddonSourceURI(item->psz_uri);
    vlc_input_policy_Release(policy);
    if (scoped) return -EACCES;
    if (sys->pool_preparser == NULL || item->psz_uri == NULL || interrupt == NULL) return VLC_EINVAL;
    struct child_engine *engine = calloc(1, sizeof(*engine)); if (engine == NULL) return VLC_ENOMEM;
    vlc_atomic_rc_init(&engine->refs); vlc_sem_init(&engine->done, 0);
    engine->pool = sys->pool_preparser; engine->cfg = *cfg; engine->status = VLC_EGENERIC;
    engine->task = preparser_task_New(sys->parent, item, VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
    if (engine->task == NULL) { child_engine_Release(engine); return VLC_ENOMEM; }
    static const struct vlc_preparser_cbs callbacks = {.on_ended=child_engine_Ended,
        .on_subtree_added=child_engine_Subtree, .on_attachments_added=child_engine_Attachments};
    const union preparser_task_cbs cbs = {.parser=&callbacks};
    const int options = VLC_PREPARSER_TYPE_PARSE | (cfg->subitems ? VLC_PREPARSER_OPTION_SUBITEMS : 0)
        | (cfg->interact ? VLC_PREPARSER_OPTION_INTERACT : 0);
    preparser_task_InitPush(engine->task, options, &cbs, engine);
    engine->task->deadline = deadline;
    engine->task->req_msg.req.baseline = input_item_Hold(item);
    vlc_interrupt_t *previous = vlc_interrupt_set(interrupt);
    vlc_interrupt_register(child_engine_Cancel, engine);
    child_engine_Hold(engine);
    int status = preparser_pool_Submit(engine->pool, engine->task);
    if (status != VLC_SUCCESS) child_engine_Release(engine);
    else {
        const bool timed_out = deadline != VLC_TICK_INVALID
            && vlc_sem_timedwait(&engine->done, deadline) != 0;
        if (timed_out) {
            child_engine_Cancel(engine); vlc_sem_wait(&engine->done); status = VLC_ETIMEOUT;
        } else {
            if (deadline == VLC_TICK_INVALID) vlc_sem_wait(&engine->done);
            status = engine->status;
        }
    }
    vlc_interrupt_unregister(); vlc_interrupt_set(previous);
    child_engine_Release(engine); return status;
}

/*****************************************************************************
 * Preparser operations
 *****************************************************************************/

/**
 * Preparser request creation for a parse request.
 * (see `vlc_preparser_req_NewParse`)
 */
static struct vlc_preparser_req *
preparser_req_NewParse(void *opaque, input_item_t *item, int options,
                       const struct vlc_preparser_cbs *cbs, void *cbs_userdata)
{
    struct preparser_sys *sys = opaque;

    assert(sys != NULL);
    assert(sys->has_requests);
    return vlc_preparser_req_NewParse(&sys->requests, item, options, cbs, cbs_userdata);
}

/**
 * Preparser request creation for a thumbnail request.
 * (see `vlc_preparser_req_NewThumbnail`)
 */
static struct vlc_preparser_req *
preparser_req_NewThumbnail(void *opaque, input_item_t *item,
                           const struct vlc_thumbnailer_arg *thumb_arg,
                           const struct vlc_thumbnailer_cbs *cbs,
                           void *cbs_userdata)
{
    struct preparser_sys *sys = opaque;

    assert(sys != NULL);
    assert(sys->pool_thumbnailer != NULL);
    assert(item != NULL);
    assert(cbs != NULL);

    const union preparser_task_cbs task_cbs = {
        .thumbnailer = cbs,
    };

    struct preparser_task *task = NULL;
    task = preparser_task_New(sys->parent, item, VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL);
    if (task == NULL) {
        return NULL;
    }
    preparser_task_InitThumbnail(task, thumb_arg, &task_cbs, cbs_userdata);

    return &task->req;
}

/**
 * Preparser request creation for a thumbnail-to-files request.
 * (see `vlc_preparser_req_NewThumbnailToFiles`)
 */
static struct vlc_preparser_req *
preparser_req_NewThumbnailToFiles(void *opaque, input_item_t *item,
                                  const struct vlc_thumbnailer_arg *thumb_arg,
                                  const struct vlc_thumbnailer_output *outputs,
                                  size_t output_count,
                                  const struct vlc_thumbnailer_to_files_cbs *cbs,
                                  void *cbs_userdata)
{
    struct preparser_sys *sys = opaque;

    assert(sys != NULL);
    assert(sys->pool_thumbnailer != NULL);
    assert(item != NULL);
    assert(cbs != NULL);

    const union preparser_task_cbs task_cbs = {
        .thumbnailer_to_files = cbs,
    };

    struct preparser_task *task = NULL;
    task = preparser_task_New(sys->parent, item,
                              VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL_TO_FILES);
    if (task == NULL) {
        return NULL;
    }
    preparser_task_InitThumbnailToFile(task, thumb_arg, outputs, output_count,
                                       &task_cbs, cbs_userdata);

    return &task->req;
}

/**
 * Preparser submit operation.
 * (see `vlc_preparser_Submit`)
 */
static int
preparser_Submit(void *opaque, struct vlc_preparser_req *req)
{
    struct preparser_sys *sys = opaque;
    assert(sys != NULL);
    if (sys->has_requests && vlc_preparser_internal_HasRequest(sys->requests.sys, req))
        return sys->requests.ops->submit(sys->requests.sys, req);
    if (req->ops->get_item != preparser_task_req_GetItem) return VLC_EINVAL;
    struct preparser_task *task = preparser_task_get_req_owner(req);

    struct preparser_process_pool *pool = NULL;
    switch (task->req_msg.req_type) {
        case VLC_PREPARSER_MSG_REQ_TYPE_PARSE:
            pool = sys->pool_preparser;
            break;
        case VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL:
        case VLC_PREPARSER_MSG_REQ_TYPE_THUMBNAIL_TO_FILES:
            pool = sys->pool_thumbnailer;
            break;
        default:
            vlc_assert_unreachable();
    }
    assert(pool != NULL);

    return preparser_pool_Submit(pool, task);
}

/**
 * Preparser Cancel operation.
 * (see `vlc_preparser_Cancel`)
 */
static size_t preparser_Cancel(void *opaque, struct vlc_preparser_req *req)
{
    struct preparser_sys *sys = opaque;
    assert(sys != NULL);

    size_t count = 0;
    if (sys->has_requests && (req == NULL || vlc_preparser_internal_HasRequest(sys->requests.sys, req))) {
        count += sys->requests.ops->cancel(sys->requests.sys, req);
        if (req != NULL) return count;
    }

    if (sys->pool_thumbnailer != NULL) {
        count += preparser_pool_Cancel(sys->pool_thumbnailer, req);
    }

    return count;
}

/**
 * Preparser Delete operation.
 * (see `vlc_preparser_Delete`)
 */
static void preparser_Delete(void *opaque)
{
    struct preparser_sys *sys = opaque;
    assert(sys != NULL);

    if (sys->has_requests) {
        sys->requests.ops->delete(sys->requests.sys); sys->has_requests = false;
    }
    if (sys->pool_preparser != NULL) {
        preparser_pool_Cancel(sys->pool_preparser, NULL);
        preparser_pool_Delete(sys->pool_preparser);
        sys->pool_preparser = NULL;
    }

    if (sys->pool_thumbnailer != NULL) {
        preparser_pool_Cancel(sys->pool_thumbnailer, NULL);
        preparser_pool_Delete(sys->pool_thumbnailer);
        sys->pool_thumbnailer = NULL;
    }

    free(sys);
}

/*****************************************************************************
 * New function
 *****************************************************************************/

/**
 * Create a new preparser with an external process.
 * (see `vlc_preparser_New`)
 */
void *vlc_preparser_external_New(vlc_preparser_t *owner, vlc_object_t *parent,
                                 const struct vlc_preparser_cfg *cfg)
{
    assert(owner != NULL);
    assert(parent != NULL);

    struct preparser_sys *sys = malloc(sizeof(*sys));
    if (sys == NULL) {
        return NULL;
    }

    sys->parent = parent;
    atomic_init(&sys->current_id, 1);
    sys->pool_preparser = NULL;
    sys->pool_thumbnailer = NULL;
    sys->has_requests = false;
    sys->requests = (struct vlc_preparser_t){0};

    if (cfg->types & VLC_PREPARSER_TYPE_PARSE) {
        size_t nprocess = cfg->max_parser_threads;
        if (nprocess == 0) {
            nprocess = 1;
        }
        sys->pool_preparser = preparser_pool_New(parent, nprocess,
                                                 cfg->timeout,
                                                 cfg->types & ~VLC_PREPARSER_TYPE_FETCHMETA_ALL);
        if (sys->pool_preparser == NULL) {
            goto end;
        }
    }

    if (cfg->types & (VLC_PREPARSER_TYPE_THUMBNAIL|
                      VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES)) {
        size_t nprocess = cfg->max_thumbnailer_threads;
        if (nprocess == 0) {
            nprocess = 1;
        }
        sys->pool_thumbnailer = preparser_pool_New(parent, nprocess,
                                                   cfg->timeout,
                                                   cfg->types & ~VLC_PREPARSER_TYPE_FETCHMETA_ALL);
        if (sys->pool_thumbnailer == NULL) {
            goto end;
        }
    }

    if (cfg->types & (VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL)) {
        struct vlc_preparser_cfg local = *cfg;
        local.types &= VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL;
        local.external_process = false;
        const struct vlc_preparser_parse_engine engine = {.run=run_child_engine, .opaque=sys};
        sys->requests.sys = vlc_preparser_internal_NewWithEngine(&sys->requests, parent, &local, &engine);
        if (sys->requests.sys == NULL) goto end;
        sys->has_requests = true;
    }

    static const struct vlc_preparser_operations ops = {
        .req_new_parse = preparser_req_NewParse,
        .req_new_thumbnail = preparser_req_NewThumbnail,
        .req_new_thumbnail_to_files = preparser_req_NewThumbnailToFiles,
        .submit = preparser_Submit,
        .cancel = preparser_Cancel,
        .delete = preparser_Delete,
    };
    owner->ops = &ops;

    return sys;
end:
    preparser_Delete(sys);
    return NULL;
}
