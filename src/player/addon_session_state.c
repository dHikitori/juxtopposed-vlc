// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_session_state.h"
#include <vlc_atomic.h>
#include <vlc_threads.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum rate_phase { RATE_NONE, RATE_RESERVED, RATE_EXECUTING, RATE_APPLIED, RATE_RESTORING };

struct vlc_addon_session_state {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    struct vlc_addon_session_snapshot value;
    uint64_t maximum_revision, rate_serial;
    unsigned maximum_tokens;
    uint32_t operations;
    enum rate_phase rate_phase;
    uint64_t rate_key, rate_executing_sequence;
    unsigned rate_pending;
    bool restore_wanted;
    vlc_tick_t restore_retry;
};

struct vlc_addon_session_token {
    vlc_atomic_rc_t refs;
    vlc_addon_session_state *state;
    struct vlc_addon_session_command command;
    struct vlc_addon_session_stamp restore_stamp;
    uint64_t lease_epoch, rate_key;
    bool attempted, begun, completed, rate_applied, pending_rate, restoring;
    bool previous_applied;
    float previous_rate;
    vlc_tick_t previous_deadline, correction_deadline;
};

static bool id_equal(vlc_addon_session_id left, vlc_addon_session_id right)
{
    return left.high == right.high && left.low == right.low;
}

static bool id_valid(vlc_addon_session_id id)
{
    return id.high != 0 || id.low != 0;
}

static bool deadline(vlc_tick_t now, vlc_tick_t duration, vlc_tick_t maximum,
                     vlc_tick_t *result)
{
    if (now <= 0 || duration <= 0 || duration > maximum || now > VLC_TICK_MAX - duration)
        return false;
    *result = now + duration;
    return true;
}

static void clear_rate(vlc_addon_session_state *state)
{
    state->rate_phase = RATE_NONE;
    state->rate_key = 0;
    state->rate_executing_sequence = 0;
    state->rate_pending = 0;
    state->restore_wanted = false;
    state->restore_retry = 0;
    state->value.rate_owned = false;
    state->value.base_rate = 0;
    state->value.corrected_rate = 0;
    state->value.rate_deadline = 0;
}

static void request_restore(vlc_addon_session_state *state)
{
    if (state->rate_phase == RATE_RESERVED)
        clear_rate(state);
    else if (state->rate_phase != RATE_NONE)
        state->restore_wanted = true;
}

static enum vlc_addon_session_status status(const vlc_addon_session_state *state)
{
    return state->value.closed ? VLC_ADDON_SESSION_CLOSED
         : state->value.exhausted ? VLC_ADDON_SESSION_EXHAUSTED : VLC_ADDON_SESSION_OK;
}

static bool counters(vlc_addon_session_state *state, bool transport, bool local,
                     bool lease, bool media, bool input)
{
    struct vlc_addon_session_snapshot *value = &state->value;
    const uint64_t maximum = state->maximum_revision;
    if ((transport && value->stamp.transport_revision == maximum)
     || (local && value->stamp.local_revision == maximum)
     || (lease && value->lease_epoch == maximum)
     || (media && value->stamp.media_generation == maximum)
     || (input && value->stamp.input_generation == maximum)) {
        value->exhausted = true;
        value->active = false;
        value->suspended = true;
        request_restore(state);
        return false;
    }
    value->stamp.transport_revision += transport;
    value->stamp.local_revision += local;
    value->lease_epoch += lease;
    value->stamp.media_generation += media;
    value->stamp.input_generation += input;
    return true;
}

static bool expected(const vlc_addon_session_state *state,
                     const struct vlc_addon_session_stamp *stamp)
{
    return stamp != NULL && id_equal(stamp->owner, state->value.stamp.owner)
        && stamp->media_generation == state->value.stamp.media_generation
        && stamp->input_generation == state->value.stamp.input_generation
        && stamp->transport_revision == state->value.stamp.transport_revision
        && stamp->local_revision == state->value.stamp.local_revision;
}

static bool scope_current(const vlc_addon_session_state *state,
                          const struct vlc_addon_session_input_scope *scope)
{
    return scope != NULL && scope->state == state && !state->value.closed
        && state->value.media && state->value.input
        && id_equal(scope->owner, state->value.stamp.owner)
        && scope->media_generation == state->value.stamp.media_generation
        && scope->input_generation == state->value.stamp.input_generation;
}

vlc_addon_session_state *vlc_addon_session_state_New(vlc_addon_session_id owner,
    const struct vlc_addon_session_limits *limits)
{
    if (!id_valid(owner) || (limits != NULL && (limits->maximum_revision < 2
     || limits->retained_tokens == 0 || limits->retained_tokens > VLC_ADDON_SESSION_MAX_TOKENS)))
        return NULL;
    vlc_addon_session_state *state = calloc(1, sizeof(*state));
    if (state == NULL)
        return NULL;
    vlc_atomic_rc_init(&state->refs);
    vlc_mutex_init(&state->lock);
    state->maximum_revision = limits != NULL ? limits->maximum_revision : UINT64_MAX;
    state->maximum_tokens = limits != NULL ? limits->retained_tokens : VLC_ADDON_SESSION_MAX_TOKENS;
    state->value.stamp = (struct vlc_addon_session_stamp) { owner, 1, 1, 1, 1 };
    state->value.lease_epoch = 1;
    state->value.origin = state->value.completed_origin = VLC_ADDON_SESSION_CORE;
    state->value.operation = state->value.completed_operation = VLC_ADDON_SESSION_OTHER;
    return state;
}

void vlc_addon_session_state_Hold(vlc_addon_session_state *state)
{
    vlc_atomic_rc_inc(&state->refs);
}

void vlc_addon_session_state_Release(vlc_addon_session_state *state)
{
    if (state != NULL && vlc_atomic_rc_dec(&state->refs))
        free(state);
}

void vlc_addon_session_state_Close(vlc_addon_session_state *state)
{
    vlc_mutex_lock(&state->lock);
    state->value.closed = true;
    state->value.active = false;
    state->value.suspended = true;
    clear_rate(state);
    vlc_mutex_unlock(&state->lock);
}

void vlc_addon_session_state_Snapshot(vlc_addon_session_state *state,
                                     struct vlc_addon_session_snapshot *result)
{
    vlc_mutex_lock(&state->lock);
    *result = state->value;
    vlc_mutex_unlock(&state->lock);
}

enum vlc_addon_session_status vlc_addon_session_state_Media(vlc_addon_session_state *state,
                                                          bool present)
{
    vlc_mutex_lock(&state->lock);
    enum vlc_addon_session_status result = status(state);
    if (result == VLC_ADDON_SESSION_OK && !counters(state, true, true, true, true, true))
        result = VLC_ADDON_SESSION_EXHAUSTED;
    if (result != VLC_ADDON_SESSION_CLOSED) {
        state->value.media = present;
        state->value.input = false;
        state->value.active = false;
        state->value.suspended = false;
        state->value.origin = VLC_ADDON_SESSION_CORE;
        state->value.operation = VLC_ADDON_SESSION_OTHER;
        clear_rate(state);
    }
    vlc_mutex_unlock(&state->lock);
    return result;
}

enum vlc_addon_session_status vlc_addon_session_state_Input(vlc_addon_session_state *state,
                                                          bool present)
{
    vlc_mutex_lock(&state->lock);
    enum vlc_addon_session_status result = status(state);
    if (result == VLC_ADDON_SESSION_OK && present && !state->value.media)
        result = VLC_ADDON_SESSION_NO_INPUT;
    if (result == VLC_ADDON_SESSION_OK && !counters(state, true, false, true, false, true))
        result = VLC_ADDON_SESSION_EXHAUSTED;
    if (result == VLC_ADDON_SESSION_OK || result == VLC_ADDON_SESSION_EXHAUSTED) {
        state->value.input = present;
        state->value.active = false;
        state->value.suspended = false;
        state->value.origin = VLC_ADDON_SESSION_CORE;
        state->value.operation = VLC_ADDON_SESSION_OTHER;
        clear_rate(state);
    }
    vlc_mutex_unlock(&state->lock);
    return result;
}

enum vlc_addon_session_status vlc_addon_session_state_Record(vlc_addon_session_state *state,
    enum vlc_addon_session_origin origin, enum vlc_addon_session_operation operation)
{
    if ((origin != VLC_ADDON_SESSION_LOCAL && origin != VLC_ADDON_SESSION_CORE)
     || operation < VLC_ADDON_SESSION_PLAY || operation > VLC_ADDON_SESSION_OTHER)
        return VLC_ADDON_SESSION_INVALID;
    vlc_mutex_lock(&state->lock);
    enum vlc_addon_session_status result = status(state);
    if (result == VLC_ADDON_SESSION_OK
     && !counters(state, true, origin == VLC_ADDON_SESSION_LOCAL, true, false, false))
        result = VLC_ADDON_SESSION_EXHAUSTED;
    if (result == VLC_ADDON_SESSION_OK || result == VLC_ADDON_SESSION_EXHAUSTED) {
        state->value.origin = origin;
        state->value.operation = operation;
        state->value.suspended = state->value.active;
        if (operation == VLC_ADDON_SESSION_USER_RATE)
            clear_rate(state);
        else
            request_restore(state);
    }
    vlc_mutex_unlock(&state->lock);
    return result;
}

enum vlc_addon_session_status vlc_addon_session_state_Rekey(vlc_addon_session_state *state,
                                                          vlc_addon_session_id owner)
{
    if (!id_valid(owner))
        return VLC_ADDON_SESSION_INVALID;
    vlc_mutex_lock(&state->lock);
    enum vlc_addon_session_status result = state->value.closed ? VLC_ADDON_SESSION_CLOSED
         : id_equal(owner, state->value.stamp.owner) ? VLC_ADDON_SESSION_INVALID
         : state->rate_phase != RATE_NONE && state->rate_phase != RATE_RESERVED
            ? VLC_ADDON_SESSION_BUSY : VLC_ADDON_SESSION_OK;
    if (result == VLC_ADDON_SESSION_OK) {
        const unsigned retained = state->value.retained_tokens;
        memset(&state->value, 0, sizeof(state->value));
        state->value.stamp = (struct vlc_addon_session_stamp) { owner, 1, 1, 1, 1 };
        state->value.lease_epoch = 1;
        state->value.origin = state->value.completed_origin = VLC_ADDON_SESSION_CORE;
        state->value.operation = state->value.completed_operation = VLC_ADDON_SESSION_OTHER;
        state->value.retained_tokens = retained;
        state->operations = 0;
        state->rate_serial = 0;
        clear_rate(state);
    }
    vlc_mutex_unlock(&state->lock);
    return result;
}

static enum vlc_addon_session_status grant(vlc_addon_session_state *state,
    const struct vlc_addon_session_stamp *stamp, vlc_addon_session_id lease,
    uint32_t operations, vlc_tick_t expires, bool resume)
{
    enum vlc_addon_session_status result = status(state);
    if (result != VLC_ADDON_SESSION_OK)
        return result;
    if (!expected(state, stamp))
        return VLC_ADDON_SESSION_STALE;
    if (!state->value.input || !state->value.media)
        return VLC_ADDON_SESSION_NO_INPUT;
    if (resume && (!state->value.active || !id_equal(lease, state->value.lease)))
        return VLC_ADDON_SESSION_DENIED;
    if (!counters(state, true, false, true, false, false))
        return VLC_ADDON_SESSION_EXHAUSTED;
    request_restore(state);
    state->value.lease = lease;
    state->value.lease_deadline = expires;
    state->value.active = true;
    state->value.suspended = false;
    if (!resume) {
        state->value.last_sequence = 0;
        state->value.last_started_sequence = 0;
    }
    state->operations = operations;
    return VLC_ADDON_SESSION_OK;
}

enum vlc_addon_session_status vlc_addon_session_state_Grant(vlc_addon_session_state *state,
    const struct vlc_addon_session_stamp *stamp, vlc_addon_session_id lease,
    uint32_t operations, vlc_tick_t now, vlc_tick_t lifetime)
{
    vlc_tick_t expires;
    if (!id_valid(lease) || operations == 0 || (operations & ~VLC_ADDON_SESSION_REMOTE_OPERATIONS)
     || !deadline(now, lifetime, VLC_ADDON_SESSION_MAX_LEASE, &expires))
        return VLC_ADDON_SESSION_INVALID;
    vlc_mutex_lock(&state->lock);
    const enum vlc_addon_session_status result = grant(state, stamp, lease, operations, expires, false);
    vlc_mutex_unlock(&state->lock);
    return result;
}

enum vlc_addon_session_status vlc_addon_session_state_Resume(vlc_addon_session_state *state,
    const struct vlc_addon_session_stamp *stamp, vlc_addon_session_id lease,
    vlc_tick_t now, vlc_tick_t lifetime)
{
    vlc_tick_t expires;
    if (!id_valid(lease) || !deadline(now, lifetime, VLC_ADDON_SESSION_MAX_LEASE, &expires))
        return VLC_ADDON_SESSION_INVALID;
    vlc_mutex_lock(&state->lock);
    const enum vlc_addon_session_status result = grant(state, stamp, lease, state->operations, expires, true);
    vlc_mutex_unlock(&state->lock);
    return result;
}

enum vlc_addon_session_status vlc_addon_session_state_Renew(vlc_addon_session_state *state,
    const struct vlc_addon_session_stamp *stamp, vlc_addon_session_id lease,
    vlc_tick_t now, vlc_tick_t lifetime)
{
    vlc_tick_t expires;
    if (!id_valid(lease) || !deadline(now, lifetime, VLC_ADDON_SESSION_MAX_LEASE, &expires))
        return VLC_ADDON_SESSION_INVALID;
    vlc_mutex_lock(&state->lock);
    enum vlc_addon_session_status result = status(state);
    if (result == VLC_ADDON_SESSION_OK && !expected(state, stamp))
        result = VLC_ADDON_SESSION_STALE;
    if (result == VLC_ADDON_SESSION_OK && (!state->value.active
     || now >= state->value.lease_deadline || !id_equal(lease, state->value.lease)))
        result = VLC_ADDON_SESSION_DENIED;
    if (result == VLC_ADDON_SESSION_OK && expires > state->value.lease_deadline)
        state->value.lease_deadline = expires;
    vlc_mutex_unlock(&state->lock);
    return result;
}

bool vlc_addon_session_state_Revoke(vlc_addon_session_state *state, vlc_addon_session_id lease)
{
    vlc_mutex_lock(&state->lock);
    const bool matched = !state->value.closed && state->value.active && id_equal(lease, state->value.lease);
    if (matched) {
        counters(state, true, false, true, false, false);
        state->value.active = false;
        state->value.suspended = true;
        request_restore(state);
    }
    vlc_mutex_unlock(&state->lock);
    return matched;
}

static bool rate_capabilities(vlc_addon_session_state *state,
    const struct vlc_addon_session_action *action, const struct vlc_addon_session_capabilities *caps)
{
    const float rate = action->parameters.rate.value;
    const float base = state->rate_key != 0 ? state->value.base_rate : caps->saved_rate;
    return caps->rate_change && !state->restore_wanted && state->rate_phase != RATE_RESTORING
        && isfinite(caps->saved_rate) && caps->saved_rate > 0
        && (state->rate_key == 0 || caps->saved_rate == state->value.base_rate)
        && isfinite(caps->effective_rate) && caps->effective_rate > 0
        && (state->rate_key != 0 || caps->effective_rate == caps->saved_rate)
        && isfinite(caps->minimum_rate) && caps->minimum_rate > 0
        && isfinite(caps->maximum_rate) && caps->maximum_rate >= caps->minimum_rate
        && isfinite(rate) && rate >= caps->minimum_rate && rate <= caps->maximum_rate
        && rate >= base * 0.95f && rate <= base * 1.05f
        && action->parameters.rate.duration > 0
        && action->parameters.rate.duration <= VLC_ADDON_SESSION_MAX_CORRECTION;
}

enum vlc_addon_session_status vlc_addon_session_state_Admit(vlc_addon_session_state *state,
    const struct vlc_addon_session_command *command,
    const struct vlc_addon_session_capabilities *caps, vlc_tick_t now,
    vlc_addon_session_token **output)
{
    if (output == NULL)
        return VLC_ADDON_SESSION_INVALID;
    *output = NULL;
    if (command == NULL || caps == NULL || now <= 0
     || command->action.operation < VLC_ADDON_SESSION_PLAY
     || command->action.operation > VLC_ADDON_SESSION_TEMPORARY_RATE || command->sequence == 0)
        return VLC_ADDON_SESSION_INVALID;
    vlc_mutex_lock(&state->lock);
    enum vlc_addon_session_status result = status(state);
    const enum vlc_addon_session_operation operation = command->action.operation;
    vlc_tick_t correction_deadline = 0;
    if (result == VLC_ADDON_SESSION_OK && !expected(state, &command->expected))
        result = VLC_ADDON_SESSION_STALE;
    if (result == VLC_ADDON_SESSION_OK && (!state->value.input || !state->value.media))
        result = VLC_ADDON_SESSION_NO_INPUT;
    if (result == VLC_ADDON_SESSION_OK && (!state->value.active || state->value.suspended
     || now >= state->value.lease_deadline || !id_equal(command->lease, state->value.lease)
     || !(state->operations & VLC_ADDON_SESSION_OPERATION_MASK(operation)) || caps->repeat_conflict))
        result = VLC_ADDON_SESSION_DENIED;
    if (result == VLC_ADDON_SESSION_OK && command->sequence <= state->value.last_sequence)
        result = VLC_ADDON_SESSION_STALE;
    if (result == VLC_ADDON_SESSION_OK
     && ((operation == VLC_ADDON_SESSION_PAUSE && !caps->pausable)
      || (operation == VLC_ADDON_SESSION_SEEK && (!caps->seekable || caps->seek_first < 0
       || caps->seek_last <= caps->seek_first || command->action.parameters.seek_time < caps->seek_first
       || command->action.parameters.seek_time > caps->seek_last))
      || (operation == VLC_ADDON_SESSION_TEMPORARY_RATE && (!rate_capabilities(state, &command->action, caps)
       || !deadline(now, command->action.parameters.rate.duration, VLC_ADDON_SESSION_MAX_CORRECTION,
                    &correction_deadline)))))
        result = VLC_ADDON_SESSION_DENIED;
    if (result == VLC_ADDON_SESSION_OK && state->value.retained_tokens >= state->maximum_tokens)
        result = VLC_ADDON_SESSION_LIMIT;
    if (result == VLC_ADDON_SESSION_OK && operation == VLC_ADDON_SESSION_TEMPORARY_RATE
     && state->rate_key == 0 && state->rate_serial == state->maximum_revision) {
        state->value.exhausted = true;
        state->value.active = false;
        state->value.suspended = true;
        request_restore(state);
        result = VLC_ADDON_SESSION_EXHAUSTED;
    }
    vlc_addon_session_token *token = NULL;
    if (result == VLC_ADDON_SESSION_OK) {
        token = calloc(1, sizeof(*token));
        if (token == NULL)
            result = VLC_ADDON_SESSION_NO_MEMORY;
    }
    if (result == VLC_ADDON_SESSION_OK && !counters(state, true, false, false, false, false))
        result = VLC_ADDON_SESSION_EXHAUSTED;
    if (result == VLC_ADDON_SESSION_OK) {
        vlc_atomic_rc_init(&token->refs);
        vlc_addon_session_state_Hold(state);
        token->state = state;
        token->command = *command;
        token->lease_epoch = state->value.lease_epoch;
        state->value.last_sequence = command->sequence;
        state->value.origin = VLC_ADDON_SESSION_REMOTE;
        state->value.operation = operation;
        ++state->value.retained_tokens;
        if (operation == VLC_ADDON_SESSION_TEMPORARY_RATE) {
            if (state->rate_key == 0) {
                state->rate_key = ++state->rate_serial;
                state->rate_phase = RATE_RESERVED;
                state->value.base_rate = caps->saved_rate;
                state->value.corrected_rate = caps->effective_rate;
                state->value.rate_owned = true;
            }
            ++state->rate_pending;
            token->pending_rate = true;
            token->rate_key = state->rate_key;
        }
        *output = token;
    } else
        free(token);
    vlc_mutex_unlock(&state->lock);
    return result;
}

bool vlc_addon_session_scope_Capture(vlc_addon_session_state *state,
                                    struct vlc_addon_session_input_scope *scope)
{
    memset(scope, 0, sizeof(*scope));
    vlc_mutex_lock(&state->lock);
    const bool valid = !state->value.closed && !state->value.exhausted
        && state->value.media && state->value.input;
    if (valid) {
        vlc_addon_session_state_Hold(state);
        *scope = (struct vlc_addon_session_input_scope) {
            state, state->value.stamp.owner, state->value.stamp.media_generation,
            state->value.stamp.input_generation,
        };
    }
    vlc_mutex_unlock(&state->lock);
    return valid;
}

void vlc_addon_session_scope_Release(struct vlc_addon_session_input_scope *scope)
{
    vlc_addon_session_state *state = scope->state;
    memset(scope, 0, sizeof(*scope));
    vlc_addon_session_state_Release(state);
}

void vlc_addon_session_token_Hold(vlc_addon_session_token *token)
{
    vlc_atomic_rc_inc(&token->refs);
}

static bool token_binding_current(const vlc_addon_session_token *token)
{
    const struct vlc_addon_session_stamp *current = &token->state->value.stamp;
    const struct vlc_addon_session_stamp *captured = &token->command.expected;
    return id_equal(current->owner, captured->owner)
        && current->media_generation == captured->media_generation
        && current->input_generation == captured->input_generation;
}

static void drop_pending(vlc_addon_session_token *token)
{
    vlc_addon_session_state *state = token->state;
    if (!token->pending_rate)
        return;
    token->pending_rate = false;
    if (token->rate_key != state->rate_key || !token_binding_current(token))
        return;
    assert(state->rate_pending > 0);
    --state->rate_pending;
    if (state->rate_pending == 0 && state->rate_phase == RATE_RESERVED)
        clear_rate(state);
}

void vlc_addon_session_token_Release(vlc_addon_session_token *token)
{
    if (token == NULL || !vlc_atomic_rc_dec(&token->refs))
        return;
    vlc_addon_session_state *state = token->state;
    vlc_mutex_lock(&state->lock);
    // Complete must precede release if a native control passed Begin.
    assert(!token->begun || token->completed);
    assert(!token->restoring);
    drop_pending(token);
    assert(state->value.retained_tokens > 0);
    --state->value.retained_tokens;
    vlc_mutex_unlock(&state->lock);
    vlc_addon_session_state_Release(state);
    free(token);
}

static bool token_valid(const vlc_addon_session_token *token,
                        const struct vlc_addon_session_input_scope *scope, vlc_tick_t now)
{
    const vlc_addon_session_state *state = token->state;
    return !token->attempted && now > 0 && scope_current(state, scope)
        && token_binding_current(token)
        && !state->value.exhausted && state->value.active && !state->value.suspended
        && now < state->value.lease_deadline && token->lease_epoch == state->value.lease_epoch
        && id_equal(token->command.lease, state->value.lease)
        && token->command.expected.local_revision == state->value.stamp.local_revision
        && token->command.sequence > state->value.last_started_sequence;
}

bool vlc_addon_session_token_Valid(vlc_addon_session_token *token,
    const struct vlc_addon_session_input_scope *scope, vlc_tick_t now)
{
    vlc_mutex_lock(&token->state->lock);
    const bool valid = token_valid(token, scope, now);
    vlc_mutex_unlock(&token->state->lock);
    return valid;
}

static bool action_equal(const struct vlc_addon_session_action *left,
                          const struct vlc_addon_session_action *right)
{
    if (right == NULL || left->operation != right->operation)
        return false;
    if (left->operation == VLC_ADDON_SESSION_SEEK)
        return left->parameters.seek_time == right->parameters.seek_time;
    if (left->operation == VLC_ADDON_SESSION_TEMPORARY_RATE)
        return left->parameters.rate.value == right->parameters.rate.value
            && left->parameters.rate.duration == right->parameters.rate.duration;
    return true;
}

bool vlc_addon_session_token_Begin(vlc_addon_session_token *token,
    const struct vlc_addon_session_input_scope *scope,
    const struct vlc_addon_session_action *actual, vlc_tick_t now)
{
    vlc_addon_session_state *state = token->state;
    vlc_mutex_lock(&state->lock);
    bool valid = token_valid(token, scope, now) && action_equal(&token->command.action, actual);
    if (valid && token->rate_key != 0)
        valid = token->rate_key == state->rate_key && !state->restore_wanted
            && (state->rate_phase == RATE_RESERVED || state->rate_phase == RATE_APPLIED)
            && deadline(now, actual->parameters.rate.duration, VLC_ADDON_SESSION_MAX_CORRECTION,
                        &token->correction_deadline);
    if (!token->attempted) {
        token->attempted = true;
        if (valid) {
            token->begun = true;
            state->value.last_started_sequence = token->command.sequence;
            if (token->rate_key != 0) {
                token->previous_applied = state->rate_phase == RATE_APPLIED;
                token->previous_rate = state->value.corrected_rate;
                token->previous_deadline = state->value.rate_deadline;
                state->rate_phase = RATE_EXECUTING;
                state->rate_executing_sequence = token->command.sequence;
            }
        }
        drop_pending(token);
    }
    vlc_mutex_unlock(&state->lock);
    return valid;
}

static void complete(vlc_addon_session_token *token, bool success, bool observed,
                     float effective_rate)
{
    vlc_addon_session_state *state = token->state;
    vlc_mutex_lock(&state->lock);
    if (token->begun && !token->completed) {
        token->completed = true;
        if (observed && token->rate_key != 0 && (!isfinite(effective_rate) || effective_rate <= 0))
            success = false;
        const bool binding = scope_current(state, &(struct vlc_addon_session_input_scope) {
                state, token->command.expected.owner, token->command.expected.media_generation,
                token->command.expected.input_generation });
        if (binding && (token->lease_epoch != state->value.completed_lease_epoch
         || !id_equal(token->command.lease, state->value.completed_lease)
         || token->command.sequence >= state->value.completed_sequence)) {
            state->value.completed_stamp = token->command.expected;
            state->value.completed_lease = token->command.lease;
            state->value.completed_lease_epoch = token->lease_epoch;
            state->value.completed_sequence = token->command.sequence;
            state->value.completed_origin = VLC_ADDON_SESSION_REMOTE;
            state->value.completed_operation = token->command.action.operation;
            state->value.completed_success = success;
        }
        if (binding && token->rate_key != 0 && token->rate_key == state->rate_key
         && state->rate_phase == RATE_EXECUTING
         && state->rate_executing_sequence == token->command.sequence) {
            const float rate = observed ? effective_rate
                : success ? token->command.action.parameters.rate.value : token->previous_rate;
            const bool finite_rate = isfinite(rate) && rate > 0;
            if (success || !finite_rate || rate != token->previous_rate) {
                state->rate_phase = RATE_APPLIED;
                token->rate_applied = true;
                state->value.corrected_rate = finite_rate ? rate : token->previous_rate;
                state->value.rate_deadline = token->correction_deadline;
                if (!success || !finite_rate || rate < state->value.base_rate * 0.95f
                 || rate > state->value.base_rate * 1.05f)
                    state->restore_wanted = true;
            } else if (token->previous_applied) {
                state->rate_phase = RATE_APPLIED;
                token->rate_applied = true;
                state->value.corrected_rate = token->previous_rate;
                state->value.rate_deadline = token->previous_deadline;
            } else if (state->rate_pending > 0 && !state->restore_wanted) {
                state->rate_phase = RATE_RESERVED;
            } else
                clear_rate(state);
        }
    }
    vlc_mutex_unlock(&state->lock);
}

void vlc_addon_session_token_Complete(vlc_addon_session_token *token, bool success)
{
    complete(token, success, false, 0);
}

void vlc_addon_session_token_CompleteRate(vlc_addon_session_token *token,
                                         bool success, float effective_rate)
{
    complete(token, success, true, effective_rate);
}

static bool rate_guard(const vlc_addon_session_token *token,
                       const struct vlc_addon_session_input_scope *scope)
{
    return token->rate_key != 0 && token->completed && token->rate_applied
        && token_binding_current(token) && scope_current(token->state, scope)
        && token->rate_key == token->state->rate_key;
}

static bool restore_due(const vlc_addon_session_state *state, vlc_tick_t now)
{
    return state->restore_wanted || !state->value.active || state->value.suspended
        || state->value.exhausted || now >= state->value.rate_deadline
        || now >= state->value.lease_deadline;
}

bool vlc_addon_session_token_RateOwned(vlc_addon_session_token *token,
    const struct vlc_addon_session_input_scope *scope)
{
    vlc_mutex_lock(&token->state->lock);
    const bool owned = rate_guard(token, scope);
    vlc_mutex_unlock(&token->state->lock);
    return owned;
}

vlc_tick_t vlc_addon_session_token_NextDeadline(vlc_addon_session_token *token,
    const struct vlc_addon_session_input_scope *scope, vlc_tick_t now)
{
    vlc_addon_session_state *state = token->state;
    vlc_mutex_lock(&state->lock);
    vlc_tick_t result = VLC_TICK_MAX;
    if (now > 0 && rate_guard(token, scope) && state->rate_phase == RATE_APPLIED) {
        if (state->restore_retry > now)
            result = state->restore_retry;
        else if (restore_due(state, now))
            result = now;
        else
            result = state->value.rate_deadline < state->value.lease_deadline
                ? state->value.rate_deadline : state->value.lease_deadline;
    }
    vlc_mutex_unlock(&state->lock);
    return result;
}

bool vlc_addon_session_token_BeginRateRestore(vlc_addon_session_token *token,
    const struct vlc_addon_session_input_scope *scope, vlc_tick_t now, float *base_rate)
{
    if (base_rate == NULL)
        return false;
    vlc_addon_session_state *state = token->state;
    vlc_mutex_lock(&state->lock);
    const bool valid = now > 0 && !token->restoring && rate_guard(token, scope)
        && state->rate_phase == RATE_APPLIED && now >= state->restore_retry && restore_due(state, now);
    if (valid) {
        state->rate_phase = RATE_RESTORING;
        token->restoring = true;
        *base_rate = state->value.base_rate;
        if (!state->value.exhausted && counters(state, true, false, false, false, false)) {
            state->value.origin = VLC_ADDON_SESSION_CORE;
            state->value.operation = VLC_ADDON_SESSION_RATE_RESTORE;
        }
        token->restore_stamp = state->value.stamp;
    }
    vlc_mutex_unlock(&state->lock);
    return valid;
}

void vlc_addon_session_token_CompleteRateRestore(vlc_addon_session_token *token,
                                               bool success, vlc_tick_t now)
{
    vlc_addon_session_state *state = token->state;
    vlc_mutex_lock(&state->lock);
    if (token->restoring) {
        token->restoring = false;
        if (token_binding_current(token) && !state->value.closed) {
            state->value.completed_stamp = token->restore_stamp;
            state->value.completed_lease = (vlc_addon_session_id) { 0, 0 };
            state->value.completed_lease_epoch = 0;
            state->value.completed_sequence = 0;
            state->value.completed_origin = VLC_ADDON_SESSION_CORE;
            state->value.completed_operation = VLC_ADDON_SESSION_RATE_RESTORE;
            state->value.completed_success = success;
        }
        if (token_binding_current(token) && token->rate_key == state->rate_key
         && state->rate_phase == RATE_RESTORING) {
            if (success)
                clear_rate(state);
            else {
                state->rate_phase = RATE_APPLIED;
                state->restore_wanted = true;
                state->restore_retry = now > 0 && now <= VLC_TICK_MAX - VLC_TICK_FROM_MS(250)
                    ? now + VLC_TICK_FROM_MS(250) : VLC_TICK_MAX;
            }
        }
    }
    vlc_mutex_unlock(&state->lock);
}
