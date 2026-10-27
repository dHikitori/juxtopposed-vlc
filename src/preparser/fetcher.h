/*****************************************************************************
 * fetcher.h
 *****************************************************************************
 * Copyright (C) 1999-2008 VLC authors and VideoLAN
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *          Clément Stenac <zorglub@videolan.org>
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

#ifndef _INPUT_FETCHER_H
#define _INPUT_FETCHER_H 1

#include <vlc_input_item.h>
#include <vlc_preparser.h>
#include <vlc_meta_fetcher.h>
#include <vlc_metadata_backend.h>

#include "metadata_pipeline.h"

/**
 * Fetcher opaque structure.
 *
 * The fetcher object will retrieve the art album data for any given input
 * item in an asynchronous way.
 */
typedef struct input_fetcher_t input_fetcher_t;

typedef struct input_fetcher_callbacks_t {
    void (*on_art_fetch_ended)(input_item_t *, bool fetched, void *userdata);
} input_fetcher_callbacks_t;

/**
 * This function creates the fetcher object and thread.
 *
 * @param request_type a combination of VLC_PREPARSER_TYPE_FETCHMETA_LOCAL and
 * VLC_PREPARSER_TYPE_FETCHMETA_NET, it is used to setup the executors for
 * each domain.
 */
input_fetcher_t *input_fetcher_New( vlc_object_t *, int request_type );

/* A native pipeline context must hold the fetcher until all stage callbacks
 * end. Delete closes and drains its executors. Release the context's hold only
 * after pipeline Delete. A late stage then sees a closed fetcher, not freed
 * storage. Release alone does not close an open fetcher.
 */
input_fetcher_t *input_fetcher_Hold(input_fetcher_t *);
void input_fetcher_Release(input_fetcher_t *);

/**
 * This function enqueues the provided item to be art fetched.
 *
 * The input item is retained until the art fetching is done or until the
 * fetcher object is destroyed.
 */
int input_fetcher_Push( input_fetcher_t *, input_item_t *, int,
                        const input_fetcher_callbacks_t *, void * );

/* The owned local stage uses only native local readers. It cannot run a Lua
 * provider or download artwork. On success, it consumes the stage through Done.
 * On error, the caller retains the stage. Cancel uses the stage as its key.
 */
int input_fetcher_PushLocal(input_fetcher_t *, vlc_metadata_pipeline_stage_t *);

/* A trusted native adapter can query one app-shipped Lua artwork family.
 * The job supplies an immutable native snapshot and a network authority flag.
 * This call copies that snapshot. It clears the old artwork claim on the copy.
 * It refuses restricted source items. It does not download or publish artwork.
 * Keep the callback context through its one terminal call. Cancel uses the job
 * as its key. It must accompany native job cancellation to stop active I/O.
 */
typedef void (*input_fetcher_probe_cb)(input_item_t *, int status, void *);
/* Run the preparser's native front stage without final publication. A legacy
 * stage requires the pipeline's never-owned decision and a separate private
 * copy. An owner transition discards that copy. The callback runs once after
 * native I/O ends. The parent then seals its local delta and calls Done. */
int input_fetcher_PushNative(input_fetcher_t *, vlc_metadata_pipeline_stage_t *,
    input_item_t *, bool legacy, input_fetcher_probe_cb, void *);
int input_fetcher_Probe(input_fetcher_t *, vlc_metadata_job_t *,
    meta_fetcher_provider_t, input_fetcher_probe_cb, void *);

/* Pass NULL to cancel all requests. This method first latches cancellation and
 * detaches queued tasks. It then interrupts native I/O and calls callbacks
 * outside the fetcher lock. A running task finishes through its own callback.
 */
size_t input_fetcher_Cancel(input_fetcher_t *, const void *request_key);

/**
 * This function destroys the fetcher object and thread.
 *
 * All pending input items will be released.
 */
void input_fetcher_Delete( input_fetcher_t * );

#endif

