// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_INPUT_ADDON_SOURCE_H
#define VLC_INPUT_ADDON_SOURCE_H 1
#include <vlc_common.h>
#include <vlc_threads.h>
#include <vlc_source_backend.h>

typedef struct {
    vlc_mutex_t lock;
    bool closed, used;
    vlc_source_job_t *pending;
    vlc_source_lease_t *lease;
    vlc_input_policy_t *policy;
} input_addon_source_t;

void input_addon_source_Init(input_addon_source_t *);
/* Stop and playback end call Close. A segment or manifest EOF does not. */
void input_addon_source_Close(input_addon_source_t *);
/* Clean follows the input thread. A late callback holds only its own state. */
void input_addon_source_Clean(input_addon_source_t *);
bool input_addon_source_IsURI(const char *);
/* The caller supplies the native primary-playback test, never package data.
 * Return an allocated loopback URI after an exact native policy transition.
 * The object must already hold the source policy before options are applied.
 * No interface event or interface-thread wait is required for cancellation.
 */
int input_addon_source_Resolve(input_addon_source_t *, vlc_object_t *, input_item_t *,
    const char *source_uri, bool primary_playback, vlc_tick_t timeout, char **proxy_uri);
#endif
