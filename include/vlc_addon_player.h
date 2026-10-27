// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_ADDON_PLAYER_H
#define VLC_ADDON_PLAYER_H
#include <vlc_player.h>
#include <vlc_tick.h>

typedef struct { uint64_t high, low; } vlc_addon_session_id;

enum vlc_addon_session_origin {
    VLC_ADDON_SESSION_LOCAL, VLC_ADDON_SESSION_REMOTE, VLC_ADDON_SESSION_CORE,
};
enum vlc_addon_session_operation {
    VLC_ADDON_SESSION_PLAY, VLC_ADDON_SESSION_PAUSE, VLC_ADDON_SESSION_SEEK,
    VLC_ADDON_SESSION_TEMPORARY_RATE, VLC_ADDON_SESSION_USER_RATE,
    VLC_ADDON_SESSION_OTHER, VLC_ADDON_SESSION_RATE_RESTORE,
};
enum vlc_addon_session_status {
    VLC_ADDON_SESSION_OK, VLC_ADDON_SESSION_INVALID, VLC_ADDON_SESSION_STALE,
    VLC_ADDON_SESSION_NO_INPUT, VLC_ADDON_SESSION_DENIED, VLC_ADDON_SESSION_BUSY,
    VLC_ADDON_SESSION_LIMIT, VLC_ADDON_SESSION_EXHAUSTED, VLC_ADDON_SESSION_CLOSED,
    VLC_ADDON_SESSION_NO_MEMORY,
};
#define VLC_ADDON_SESSION_OPERATION_MASK(op) (UINT32_C(1) << (op))
#define VLC_ADDON_SESSION_REMOTE_OPERATIONS ((UINT32_C(1) << 4) - 1)
#define VLC_ADDON_SESSION_MAX_TOKENS 64
#define VLC_ADDON_SESSION_MAX_LEASE VLC_TICK_FROM_SEC(60)
#define VLC_ADDON_SESSION_MAX_CORRECTION VLC_TICK_FROM_SEC(5)

struct vlc_addon_session_stamp {
    vlc_addon_session_id owner;
    uint64_t media_generation, input_generation, transport_revision, local_revision;
};
struct vlc_addon_session_action {
    enum vlc_addon_session_operation operation;
    union {
        vlc_tick_t seek_time;
        struct { float value; vlc_tick_t duration; } rate;
    } parameters;
};
struct vlc_addon_session_command {
    struct vlc_addon_session_stamp expected;
    vlc_addon_session_id lease;
    uint64_t sequence;
    struct vlc_addon_session_action action;
};
struct vlc_addon_session_capabilities {
    bool pausable, seekable, rate_change, repeat_conflict;
    vlc_tick_t seek_first, seek_last;
    // The saved rate is distinct from the effective rate during a correction.
    float saved_rate, effective_rate, minimum_rate, maximum_rate;
};
struct vlc_addon_session_snapshot {
    struct vlc_addon_session_stamp stamp;
    struct vlc_addon_session_stamp completed_stamp;
    vlc_addon_session_id lease;
    vlc_addon_session_id completed_lease;
    uint64_t lease_epoch, last_sequence, last_started_sequence, completed_sequence;
    uint64_t completed_lease_epoch;
    enum vlc_addon_session_origin origin, completed_origin;
    enum vlc_addon_session_operation operation, completed_operation;
    bool media, input, active, suspended, closed, exhausted, completed_success;
    bool rate_owned;
    float base_rate, corrected_rate;
    vlc_tick_t rate_deadline, lease_deadline;
    unsigned retained_tokens;
};
// All methods require the native player lock. The host assigns lease IDs.
// A worker cannot call this API or choose a native command origin.
VLC_API void vlc_addon_player_Snapshot(vlc_player_t *, struct vlc_addon_session_snapshot *);
VLC_API enum vlc_addon_session_status vlc_addon_player_Grant(vlc_player_t *,
    const struct vlc_addon_session_stamp *, vlc_addon_session_id lease, uint32_t operations, vlc_tick_t lifetime);
VLC_API enum vlc_addon_session_status vlc_addon_player_Renew(vlc_player_t *,
    const struct vlc_addon_session_stamp *, vlc_addon_session_id lease, vlc_tick_t lifetime);
VLC_API enum vlc_addon_session_status vlc_addon_player_Resume(vlc_player_t *,
    const struct vlc_addon_session_stamp *, vlc_addon_session_id lease, vlc_tick_t lifetime);
VLC_API bool vlc_addon_player_Revoke(vlc_player_t *, vlc_addon_session_id lease);
VLC_API enum vlc_addon_session_status vlc_addon_player_Apply(vlc_player_t *,
    const struct vlc_addon_session_command *);
typedef struct vlc_addon_player_listener vlc_addon_player_listener;
// Completion runs under the player lock. Copy the value and defer interface
// work. Do not call the player, add/remove listeners, or destroy it here.
typedef void (*vlc_addon_player_completion)(const struct vlc_addon_session_snapshot *, void *);
VLC_API vlc_addon_player_listener *vlc_addon_player_AddListener(vlc_player_t *,
    vlc_addon_player_completion, void *);
VLC_API void vlc_addon_player_RemoveListener(vlc_player_t *, vlc_addon_player_listener *);
#endif
