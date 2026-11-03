// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_PLAYER_ADDON_SESSION_STATE_H
#define VLC_PLAYER_ADDON_SESSION_STATE_H
#include <vlc_common.h>
#include <vlc_addon_player.h>

typedef struct vlc_addon_session_state vlc_addon_session_state;
typedef struct vlc_addon_session_token vlc_addon_session_token;
// Keep one immutable scope for each native input. It holds the state, not a
// player or interface pointer. Release it after the input has released controls.
struct vlc_addon_session_input_scope {
    vlc_addon_session_state *state;
    vlc_addon_session_id owner;
    uint64_t media_generation, input_generation;
};
struct vlc_addon_session_limits {
    uint64_t maximum_revision;
    unsigned retained_tokens;
};

vlc_addon_session_state *vlc_addon_session_state_New(vlc_addon_session_id owner,
    const struct vlc_addon_session_limits *);
void vlc_addon_session_state_Hold(vlc_addon_session_state *);
void vlc_addon_session_state_Release(vlc_addon_session_state *);
void vlc_addon_session_state_Close(vlc_addon_session_state *);
void vlc_addon_session_state_Snapshot(vlc_addon_session_state *, struct vlc_addon_session_snapshot *);

// The native player lock serializes these authority methods. They release their
// internal mutex before the caller wakes or writes the input FIFO.
// Record accepts local or core origin. Admission assigns remote origin itself.
enum vlc_addon_session_status vlc_addon_session_state_Media(vlc_addon_session_state *, bool present);
enum vlc_addon_session_status vlc_addon_session_state_Input(vlc_addon_session_state *, bool present);
enum vlc_addon_session_status vlc_addon_session_state_Record(vlc_addon_session_state *,
    enum vlc_addon_session_origin, enum vlc_addon_session_operation);
// Restore an applied correction before rekey. Pending tokens can keep the old
// owner alive, but cannot obtain authority in the new owner or input scope.
enum vlc_addon_session_status vlc_addon_session_state_Rekey(vlc_addon_session_state *, vlc_addon_session_id owner);
enum vlc_addon_session_status vlc_addon_session_state_Grant(vlc_addon_session_state *,
    const struct vlc_addon_session_stamp *, vlc_addon_session_id lease, uint32_t operations, vlc_tick_t now, vlc_tick_t lifetime);
enum vlc_addon_session_status vlc_addon_session_state_Resume(vlc_addon_session_state *,
    const struct vlc_addon_session_stamp *, vlc_addon_session_id lease, vlc_tick_t now, vlc_tick_t lifetime);
enum vlc_addon_session_status vlc_addon_session_state_Renew(vlc_addon_session_state *,
    const struct vlc_addon_session_stamp *, vlc_addon_session_id lease, vlc_tick_t now, vlc_tick_t lifetime);
bool vlc_addon_session_state_Revoke(vlc_addon_session_state *, vlc_addon_session_id lease);
enum vlc_addon_session_status vlc_addon_session_state_Admit(vlc_addon_session_state *,
    const struct vlc_addon_session_command *, const struct vlc_addon_session_capabilities *, vlc_tick_t now,
    vlc_addon_session_token **);
bool vlc_addon_session_scope_Capture(vlc_addon_session_state *, struct vlc_addon_session_input_scope *);
void vlc_addon_session_scope_Release(struct vlc_addon_session_input_scope *);

void vlc_addon_session_token_Hold(vlc_addon_session_token *);
void vlc_addon_session_token_Release(vlc_addon_session_token *);
bool vlc_addon_session_token_Valid(vlc_addon_session_token *, const struct vlc_addon_session_input_scope *, vlc_tick_t now);
bool vlc_addon_session_token_RateOwned(vlc_addon_session_token *, const struct vlc_addon_session_input_scope *);
// Begin is one-use and arbitrates with local input before any native mutation.
// A later local action follows this claimed FIFO step. No player lock is taken.
// Call Complete before the input releases a token that passed Begin.
bool vlc_addon_session_token_Begin(vlc_addon_session_token *, const struct vlc_addon_session_input_scope *,
    const struct vlc_addon_session_action *actual, vlc_tick_t now);
void vlc_addon_session_token_Complete(vlc_addon_session_token *, bool success);
// A demux can adjust the requested rate. Native rate controls must report the
// effective input rate, including after failure, so cleanup keeps ownership of
// a partial correction. Complete alone assumes no effect after rate failure.
void vlc_addon_session_token_CompleteRate(vlc_addon_session_token *, bool success, float effective_rate);

// The input retains a successful rate token. Restore uses no new allocation or
// FIFO slot. User rate edits, a new input, and close invalidate this guard.
// State Close is for player destruction. A session end uses Revoke, which keeps
// the same input's restore guard valid until its rate returns to the saved rate.
vlc_tick_t vlc_addon_session_token_NextDeadline(vlc_addon_session_token *, const struct vlc_addon_session_input_scope *, vlc_tick_t now);
bool vlc_addon_session_token_BeginRateRestore(vlc_addon_session_token *, const struct vlc_addon_session_input_scope *, vlc_tick_t now, float *base_rate);
// Success means that the effective input rate equals the returned base rate.
// A generic Control return value does not establish that result.
void vlc_addon_session_token_CompleteRateRestore(vlc_addon_session_token *, bool success, vlc_tick_t now);
#endif
