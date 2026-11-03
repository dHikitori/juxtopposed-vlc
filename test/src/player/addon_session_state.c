// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include "../../../src/player/addon_session_state.h"
#include <vlc_threads.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

// These tests exercise the private state boundary. They do not mount native
// player hooks or prove input FIFO integration.
static const vlc_addon_session_id owner = { 10, 20 }, lease = { 30, 40 };
static const vlc_tick_t now = VLC_TICK_FROM_SEC(10);

struct fixture {
    vlc_addon_session_state *state;
    struct vlc_addon_session_input_scope scope;
    struct vlc_addon_session_capabilities caps;
};

static struct vlc_addon_session_snapshot snapshot(vlc_addon_session_state *state)
{
    struct vlc_addon_session_snapshot result;
    vlc_addon_session_state_Snapshot(state, &result);
    return result;
}

static void grant_current(vlc_addon_session_state *state)
{
    struct vlc_addon_session_snapshot value = snapshot(state);
    assert(vlc_addon_session_state_Grant(state, &value.stamp, lease,
        VLC_ADDON_SESSION_REMOTE_OPERATIONS, now, VLC_TICK_FROM_SEC(30)) == VLC_ADDON_SESSION_OK);
}

static void fixture_init(struct fixture *fixture, const struct vlc_addon_session_limits *limits)
{
    *fixture = (struct fixture) {
        .state = vlc_addon_session_state_New(owner, limits),
        .caps = { .pausable = true, .seekable = true, .rate_change = true,
            .seek_first = 0, .seek_last = VLC_TICK_FROM_SEC(100), .saved_rate = 1,
            .effective_rate = 1, .minimum_rate = 0.5f, .maximum_rate = 2 },
    };
    assert(fixture->state != NULL);
    assert(vlc_addon_session_state_Media(fixture->state, true) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Input(fixture->state, true) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_scope_Capture(fixture->state, &fixture->scope));
    grant_current(fixture->state);
}

static void fixture_destroy(struct fixture *fixture)
{
    assert(snapshot(fixture->state).retained_tokens == 0);
    vlc_addon_session_state_Close(fixture->state);
    vlc_addon_session_scope_Release(&fixture->scope);
    vlc_addon_session_state_Release(fixture->state);
}

static struct vlc_addon_session_action simple(enum vlc_addon_session_operation operation)
{
    return (struct vlc_addon_session_action) { .operation = operation };
}

static struct vlc_addon_session_action correction(float value, vlc_tick_t duration)
{
    return (struct vlc_addon_session_action) {
        .operation = VLC_ADDON_SESSION_TEMPORARY_RATE,
        .parameters.rate = { value, duration },
    };
}

static struct vlc_addon_session_command command(struct fixture *fixture, uint64_t sequence,
                                                struct vlc_addon_session_action action)
{
    return (struct vlc_addon_session_command) {
        .expected = snapshot(fixture->state).stamp, .lease = lease,
        .sequence = sequence, .action = action,
    };
}

static vlc_addon_session_token *admit(struct fixture *fixture, uint64_t sequence,
                                     struct vlc_addon_session_action action)
{
    struct vlc_addon_session_command request = command(fixture, sequence, action);
    vlc_addon_session_token *token = NULL;
    assert(vlc_addon_session_state_Admit(fixture->state, &request, &fixture->caps,
        now, &token) == VLC_ADDON_SESSION_OK);
    assert(token != NULL);
    return token;
}

static vlc_addon_session_token *apply_rate(struct fixture *fixture, uint64_t sequence,
                                          float value, vlc_tick_t duration)
{
    struct vlc_addon_session_action action = correction(value, duration);
    vlc_addon_session_token *token = admit(fixture, sequence, action);
    assert(vlc_addon_session_token_Begin(token, &fixture->scope, &action, now));
    vlc_addon_session_token_Complete(token, true);
    return token;
}

static void test_admission_uses_exact_stamp(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    struct vlc_addon_session_command request = command(&fixture, 1, simple(VLC_ADDON_SESSION_PAUSE));
    const struct vlc_addon_session_snapshot before = snapshot(fixture.state);
    vlc_addon_session_token *token = NULL;
    for (unsigned field = 0; field < 5; ++field) {
        struct vlc_addon_session_command bad = request;
        switch (field) {
            case 0: ++bad.expected.owner.high; break;
            case 1: ++bad.expected.media_generation; break;
            case 2: ++bad.expected.input_generation; break;
            case 3: ++bad.expected.transport_revision; break;
            case 4: ++bad.expected.local_revision; break;
        }
        assert(vlc_addon_session_state_Admit(fixture.state, &bad, &fixture.caps, now,
            &token) == VLC_ADDON_SESSION_STALE);
        assert(token == NULL);
    }
    assert(snapshot(fixture.state).stamp.transport_revision == before.stamp.transport_revision);
    assert(vlc_addon_session_state_Admit(fixture.state, &request, &fixture.caps, now,
        &token) == VLC_ADDON_SESSION_OK);
    const struct vlc_addon_session_snapshot after = snapshot(fixture.state);
    assert(after.origin == VLC_ADDON_SESSION_REMOTE && after.last_sequence == 1);
    assert(after.stamp.local_revision == before.stamp.local_revision);
    assert(after.stamp.transport_revision == before.stamp.transport_revision + 1);
    // A copied task cannot reuse the old admission stamp.
    assert(vlc_addon_session_state_Admit(fixture.state, &request, &fixture.caps, now,
        &(vlc_addon_session_token *){ NULL }) == VLC_ADDON_SESSION_STALE);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_payload_is_exact_and_one_use(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    struct vlc_addon_session_action action = simple(VLC_ADDON_SESSION_SEEK);
    action.parameters.seek_time = VLC_TICK_FROM_SEC(20);
    vlc_addon_session_token *token = admit(&fixture, 1, action);
    struct vlc_addon_session_action wrong = action;
    ++wrong.parameters.seek_time;
    assert(!vlc_addon_session_token_Begin(token, &fixture.scope, &wrong, now));
    assert(!vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
    vlc_addon_session_token_Release(token);
    token = admit(&fixture, 2, action);
    assert(vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
    assert(!vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
    vlc_addon_session_token_Complete(token, true);
    vlc_addon_session_token_Complete(token, false);
    assert(snapshot(fixture.state).completed_success);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

struct queued_begin {
    struct fixture *fixture;
    vlc_addon_session_token *token;
    struct vlc_addon_session_action action;
    vlc_sem_t begin, entered, finish;
    bool result;
};

static void *begin_thread(void *opaque)
{
    struct queued_begin *queued = opaque;
    vlc_sem_wait(&queued->begin);
    queued->result = vlc_addon_session_token_Begin(queued->token,
        &queued->fixture->scope, &queued->action, now);
    vlc_sem_post(&queued->entered);
    vlc_sem_wait(&queued->finish);
    if (queued->result)
        vlc_addon_session_token_Complete(queued->token, true);
    return NULL;
}

static void test_local_priority_before_or_after_begin(void)
{
    for (unsigned local_first = 0; local_first < 2; ++local_first) {
        struct fixture fixture;
        fixture_init(&fixture, NULL);
        struct queued_begin queued = { .fixture = &fixture,
            .action = simple(VLC_ADDON_SESSION_PAUSE) };
        queued.token = admit(&fixture, 1, queued.action);
        vlc_sem_init(&queued.begin, 0); vlc_sem_init(&queued.entered, 0);
        vlc_sem_init(&queued.finish, 0);
        vlc_thread_t thread;
        assert(vlc_clone(&thread, begin_thread, &queued) == 0);
        if (local_first)
            assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
                VLC_ADDON_SESSION_PAUSE) == VLC_ADDON_SESSION_OK);
        vlc_sem_post(&queued.begin); vlc_sem_wait(&queued.entered);
        assert(queued.result == !local_first);
        if (!local_first)
            assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
                VLC_ADDON_SESSION_PAUSE) == VLC_ADDON_SESSION_OK);
        vlc_sem_post(&queued.finish); vlc_join(thread, NULL);
        const struct vlc_addon_session_snapshot value = snapshot(fixture.state);
        assert(value.suspended && value.origin == VLC_ADDON_SESSION_LOCAL);
        assert(value.operation == VLC_ADDON_SESSION_PAUSE);
        if (!local_first) {
            assert(value.completed_origin == VLC_ADDON_SESSION_REMOTE);
            assert(value.completed_sequence == 1);
            assert(value.completed_stamp.local_revision < value.stamp.local_revision);
            assert(value.completed_lease.high == lease.high && value.completed_lease.low == lease.low);
        }
        assert(!vlc_addon_session_token_Valid(queued.token, &fixture.scope, now));
        vlc_addon_session_token_Release(queued.token);
        fixture_destroy(&fixture);
    }
}

static void test_revoke_and_owner_close(void)
{
    for (unsigned close_owner = 0; close_owner < 2; ++close_owner) {
        struct fixture fixture;
        fixture_init(&fixture, NULL);
        const struct vlc_addon_session_action action = simple(VLC_ADDON_SESSION_PLAY);
        vlc_addon_session_token *token = admit(&fixture, 1, action);
        if (close_owner)
            vlc_addon_session_state_Close(fixture.state);
        else {
            assert(!vlc_addon_session_state_Revoke(fixture.state, (vlc_addon_session_id) { 70, 80 }));
            assert(vlc_addon_session_token_Valid(token, &fixture.scope, now));
            assert(vlc_addon_session_state_Revoke(fixture.state, lease));
        }
        // Scope and token retain the plain Core owner after its caller releases it.
        vlc_addon_session_state_Release(fixture.state);
        assert(!vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
        vlc_addon_session_token_Release(token);
        vlc_addon_session_scope_Release(&fixture.scope);
    }
}

static void test_later_admission_preserves_fifo_order(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    const struct vlc_addon_session_action action = simple(VLC_ADDON_SESSION_PLAY);
    vlc_addon_session_token *first = admit(&fixture, 1, action);
    vlc_addon_session_token *second = admit(&fixture, 2, action);
    assert(vlc_addon_session_token_Valid(first, &fixture.scope, now));
    assert(vlc_addon_session_token_Begin(first, &fixture.scope, &action, now));
    vlc_addon_session_token_Complete(first, true);
    assert(vlc_addon_session_token_Begin(second, &fixture.scope, &action, now));
    vlc_addon_session_token_Complete(second, true);
    vlc_addon_session_token_Release(first); vlc_addon_session_token_Release(second);
    first = admit(&fixture, 3, action); second = admit(&fixture, 4, action);
    assert(vlc_addon_session_token_Begin(second, &fixture.scope, &action, now));
    vlc_addon_session_token_Complete(second, true);
    assert(!vlc_addon_session_token_Begin(first, &fixture.scope, &action, now));
    assert(snapshot(fixture.state).completed_sequence == 4);
    vlc_addon_session_token_Release(first); vlc_addon_session_token_Release(second);
    fixture_destroy(&fixture);
}

static void test_resume_requires_current_authority(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    vlc_addon_session_token *old = admit(&fixture, 10, simple(VLC_ADDON_SESSION_PLAY));
    const struct vlc_addon_session_stamp before = snapshot(fixture.state).stamp;
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
        VLC_ADDON_SESSION_PAUSE) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Resume(fixture.state, &before, lease, now,
        VLC_TICK_FROM_SEC(30)) == VLC_ADDON_SESSION_STALE);
    const struct vlc_addon_session_stamp current = snapshot(fixture.state).stamp;
    assert(vlc_addon_session_state_Resume(fixture.state, &current, lease, now,
        VLC_TICK_FROM_SEC(30)) == VLC_ADDON_SESSION_OK);
    assert(!vlc_addon_session_token_Valid(old, &fixture.scope, now));
    struct vlc_addon_session_command duplicate = command(&fixture, 10, simple(VLC_ADDON_SESSION_PLAY));
    vlc_addon_session_token *token = NULL;
    assert(vlc_addon_session_state_Admit(fixture.state, &duplicate, &fixture.caps, now,
        &token) == VLC_ADDON_SESSION_STALE);
    vlc_addon_session_token_Release(old);
    fixture_destroy(&fixture);
}

static void test_scope_reuse_and_foreign_owner(void)
{
    struct fixture fixture, other;
    fixture_init(&fixture, NULL); fixture_init(&other, NULL);
    const struct vlc_addon_session_action action = simple(VLC_ADDON_SESSION_PLAY);
    vlc_addon_session_token *token = admit(&fixture, 1, action);
    assert(!vlc_addon_session_token_Valid(token, &other.scope, now));
    assert(vlc_addon_session_state_Input(fixture.state, false) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Input(fixture.state, true) == VLC_ADDON_SESSION_OK);
    struct vlc_addon_session_input_scope replacement;
    assert(vlc_addon_session_scope_Capture(fixture.state, &replacement));
    grant_current(fixture.state);
    assert(!vlc_addon_session_token_Begin(token, &replacement, &action, now));
    assert(replacement.input_generation != fixture.scope.input_generation);
    vlc_addon_session_scope_Release(&replacement);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture); fixture_destroy(&other);
}

static void test_core_origin_stays_separate(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    vlc_addon_session_token *token = admit(&fixture, 1, simple(VLC_ADDON_SESSION_PLAY));
    const struct vlc_addon_session_snapshot before = snapshot(fixture.state);
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_CORE,
        VLC_ADDON_SESSION_SEEK) == VLC_ADDON_SESSION_OK);
    const struct vlc_addon_session_snapshot after = snapshot(fixture.state);
    assert(after.origin == VLC_ADDON_SESSION_CORE && after.suspended);
    assert(after.stamp.local_revision == before.stamp.local_revision);
    assert(after.stamp.transport_revision == before.stamp.transport_revision + 1);
    assert(!vlc_addon_session_token_Valid(token, &fixture.scope, now));
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_REMOTE,
        VLC_ADDON_SESSION_PLAY) == VLC_ADDON_SESSION_INVALID);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_closed_capabilities_and_parameters(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    const struct vlc_addon_session_snapshot before = snapshot(fixture.state);
    for (unsigned invalid = 0; invalid < 12; ++invalid) {
        struct vlc_addon_session_command request = command(&fixture, 1,
            correction(1.02f, VLC_TICK_FROM_SEC(1)));
        struct vlc_addon_session_capabilities caps = fixture.caps;
        switch (invalid) {
            case 0: request.action.parameters.rate.value = NAN; break;
            case 1: request.action.parameters.rate.value = 1.06f; break;
            case 2: request.action.parameters.rate.duration = VLC_ADDON_SESSION_MAX_CORRECTION + 1; break;
            case 3: request.action.parameters.rate.duration = 0; break;
            case 4: caps.rate_change = false; break;
            case 5: caps.minimum_rate = NAN; break;
            case 6: caps.saved_rate = INFINITY; break;
            case 7: caps.repeat_conflict = true; break;
            case 8: request.action = simple(VLC_ADDON_SESSION_PAUSE); caps.pausable = false; break;
            case 9: request.action = simple(VLC_ADDON_SESSION_SEEK); caps.seekable = false; break;
            case 10: request.action = simple(VLC_ADDON_SESSION_SEEK);
                request.action.parameters.seek_time = caps.seek_last + 1; break;
            case 11: request.action = simple(VLC_ADDON_SESSION_SEEK);
                request.action.parameters.seek_time = -1; break;
        }
        vlc_addon_session_token *token = NULL;
        assert(vlc_addon_session_state_Admit(fixture.state, &request, &caps, now,
            &token) == VLC_ADDON_SESSION_DENIED);
        assert(token == NULL);
    }
    assert(snapshot(fixture.state).stamp.transport_revision == before.stamp.transport_revision);
    fixture_destroy(&fixture);
}

static void test_lease_bounds_and_renewal(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    vlc_addon_session_token *token = admit(&fixture, 1, simple(VLC_ADDON_SESSION_PLAY));
    struct vlc_addon_session_snapshot current = snapshot(fixture.state);
    assert(vlc_addon_session_state_Renew(fixture.state, &current.stamp, lease,
        now + VLC_TICK_FROM_SEC(1), VLC_TICK_FROM_SEC(30)) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Renew(fixture.state, &current.stamp, lease,
        now + VLC_TICK_FROM_SEC(1), VLC_TICK_FROM_MS(1)) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_token_Valid(token, &fixture.scope, now + VLC_TICK_FROM_SEC(30)));
    assert(snapshot(fixture.state).last_sequence == 1);
    assert(!vlc_addon_session_token_Valid(token, &fixture.scope, now + VLC_TICK_FROM_SEC(31)));
    current = snapshot(fixture.state);
    assert(vlc_addon_session_state_Renew(fixture.state, &current.stamp, lease,
        now + VLC_TICK_FROM_SEC(31), VLC_TICK_FROM_SEC(30)) == VLC_ADDON_SESSION_DENIED);
    assert(vlc_addon_session_state_Grant(fixture.state, &current.stamp, lease,
        VLC_ADDON_SESSION_REMOTE_OPERATIONS, now, VLC_ADDON_SESSION_MAX_LEASE + 1) == VLC_ADDON_SESSION_INVALID);
    assert(vlc_addon_session_state_Grant(fixture.state, &current.stamp, lease,
        VLC_ADDON_SESSION_REMOTE_OPERATIONS, VLC_TICK_MAX - 2, 3) == VLC_ADDON_SESSION_INVALID);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_rate_restore_after_end_or_seek(void)
{
    for (unsigned end = 0; end < 2; ++end) {
        struct fixture fixture;
        fixture_init(&fixture, NULL);
        vlc_addon_session_token *token = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(2));
        assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope, now) == now + VLC_TICK_FROM_SEC(2));
        if (end)
            assert(vlc_addon_session_state_Revoke(fixture.state, lease));
        else
            assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
                VLC_ADDON_SESSION_SEEK) == VLC_ADDON_SESSION_OK);
        assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope, now) == now);
        float base = 0;
        assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, now, &base));
        assert(base == 1);
        assert(!vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, now, &base));
        vlc_addon_session_token_CompleteRateRestore(token, true, now);
        assert(!snapshot(fixture.state).rate_owned);
        assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope, now) == VLC_TICK_MAX);
        vlc_addon_session_token_Release(token);
        fixture_destroy(&fixture);
    }
}

static void test_rate_restore_uses_no_spare_token(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    vlc_addon_session_token *tokens[VLC_ADDON_SESSION_MAX_TOKENS];
    tokens[0] = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(1));
    for (unsigned index = 1; index < ARRAY_SIZE(tokens); ++index)
        tokens[index] = admit(&fixture, index + 1, simple(VLC_ADDON_SESSION_PLAY));
    vlc_addon_session_token *overflow = NULL;
    struct vlc_addon_session_command request = command(&fixture, ARRAY_SIZE(tokens) + 1,
        simple(VLC_ADDON_SESSION_PLAY));
    assert(vlc_addon_session_state_Admit(fixture.state, &request, &fixture.caps, now,
        &overflow) == VLC_ADDON_SESSION_LIMIT);
    assert(snapshot(fixture.state).retained_tokens == ARRAY_SIZE(tokens));
    float base = 0;
    assert(vlc_addon_session_token_BeginRateRestore(tokens[0], &fixture.scope,
        now + VLC_TICK_FROM_SEC(1), &base));
    assert(base == 1);
    vlc_addon_session_token_CompleteRateRestore(tokens[0], true, now + VLC_TICK_FROM_SEC(1));
    assert(snapshot(fixture.state).retained_tokens == ARRAY_SIZE(tokens));
    for (unsigned index = 0; index < ARRAY_SIZE(tokens); ++index)
        vlc_addon_session_token_Release(tokens[index]);
    fixture_destroy(&fixture);
}

static void test_user_rate_wins_before_or_after_apply(void)
{
    for (unsigned begun = 0; begun < 2; ++begun) {
        struct fixture fixture;
        fixture_init(&fixture, NULL);
        const struct vlc_addon_session_action action = correction(1.02f, VLC_TICK_FROM_SEC(1));
        vlc_addon_session_token *token = admit(&fixture, 1, action);
        if (begun)
            assert(vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
        // Even an equal user rate is a new local ownership choice.
        assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
            VLC_ADDON_SESSION_USER_RATE) == VLC_ADDON_SESSION_OK);
        if (begun)
            vlc_addon_session_token_Complete(token, true);
        else
            assert(!vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
        float base = 0;
        assert(!vlc_addon_session_token_BeginRateRestore(token, &fixture.scope,
            now + VLC_TICK_FROM_SEC(2), &base));
        assert(!snapshot(fixture.state).rate_owned);
        vlc_addon_session_token_Release(token);
        fixture_destroy(&fixture);
    }
}

static void test_seek_during_correction_apply_requests_restore(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    const struct vlc_addon_session_action action = correction(1.02f, VLC_TICK_FROM_SEC(3));
    vlc_addon_session_token *token = admit(&fixture, 1, action);
    assert(vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
        VLC_ADDON_SESSION_SEEK) == VLC_ADDON_SESSION_OK);
    vlc_addon_session_token_Complete(token, true);
    float base = 0;
    assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, now, &base));
    assert(base == 1);
    vlc_addon_session_token_CompleteRateRestore(token, true, now);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_rate_updates_keep_original_base_and_failure_state(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    fixture.caps.saved_rate = fixture.caps.effective_rate = 1.25f;
    vlc_addon_session_token *first = apply_rate(&fixture, 1, 1.27f, VLC_TICK_FROM_SEC(2));
    fixture.caps.effective_rate = 1.27f;
    struct vlc_addon_session_action action = correction(1.28f, VLC_TICK_FROM_SEC(3));
    vlc_addon_session_token *failed = admit(&fixture, 2, action);
    assert(vlc_addon_session_token_Begin(failed, &fixture.scope, &action, now));
    vlc_addon_session_token_Complete(failed, false);
    assert(snapshot(fixture.state).corrected_rate == 1.27f);
    assert(snapshot(fixture.state).rate_deadline == now + VLC_TICK_FROM_SEC(2));
    vlc_addon_session_token *latest = apply_rate(&fixture, 3, 1.28f, VLC_TICK_FROM_SEC(3));
    assert(snapshot(fixture.state).base_rate == 1.25f);
    assert(snapshot(fixture.state).corrected_rate == 1.28f);
    assert(vlc_addon_session_state_Revoke(fixture.state, lease));
    float base = 0;
    assert(vlc_addon_session_token_BeginRateRestore(latest, &fixture.scope, now, &base));
    assert(base == 1.25f);
    vlc_addon_session_token_CompleteRateRestore(latest, true, now);
    vlc_addon_session_token_Release(first); vlc_addon_session_token_Release(failed);
    vlc_addon_session_token_Release(latest);
    fixture_destroy(&fixture);
}

static void test_restore_failure_has_bounded_retry(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    vlc_addon_session_token *token = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(1));
    const vlc_tick_t expired = now + VLC_TICK_FROM_SEC(1);
    float base = 0;
    assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, expired, &base));
    vlc_addon_session_token_CompleteRateRestore(token, false, expired);
    assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope, expired) == expired + VLC_TICK_FROM_MS(250));
    assert(!vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, expired, &base));
    assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope,
        expired + VLC_TICK_FROM_MS(250), &base));
    vlc_addon_session_token_CompleteRateRestore(token, true, expired + VLC_TICK_FROM_MS(250));
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_old_rate_tokens_cannot_touch_new_owner(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    const struct vlc_addon_session_action action = correction(1.02f, VLC_TICK_FROM_SEC(2));
    vlc_addon_session_token *pending = admit(&fixture, 1, action);
    vlc_addon_session_token *executing = admit(&fixture, 2, action);
    assert(vlc_addon_session_token_Begin(executing, &fixture.scope, &action, now));
    assert(vlc_addon_session_state_Rekey(fixture.state, (vlc_addon_session_id) { 110, 220 }) == VLC_ADDON_SESSION_BUSY);
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
        VLC_ADDON_SESSION_USER_RATE) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Rekey(fixture.state, (vlc_addon_session_id) { 110, 220 }) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Media(fixture.state, true) == VLC_ADDON_SESSION_OK);
    assert(vlc_addon_session_state_Input(fixture.state, true) == VLC_ADDON_SESSION_OK);
    vlc_addon_session_scope_Release(&fixture.scope);
    assert(vlc_addon_session_scope_Capture(fixture.state, &fixture.scope));
    grant_current(fixture.state);
    vlc_addon_session_token *replacement_pending = admit(&fixture, 1, action);
    vlc_addon_session_token *replacement = admit(&fixture, 2, action);
    assert(vlc_addon_session_token_Begin(replacement, &fixture.scope, &action, now));
    // Keys and sequence numbers can repeat after a fresh owner identity.
    vlc_addon_session_token_Release(pending);
    vlc_addon_session_token_Complete(executing, true);
    vlc_addon_session_token_Release(executing);
    assert(snapshot(fixture.state).rate_owned);
    vlc_addon_session_token_Complete(replacement, true);
    assert(snapshot(fixture.state).corrected_rate == 1.02f);
    vlc_addon_session_token_Release(replacement_pending);
    vlc_addon_session_token_Release(replacement);
    fixture_destroy(&fixture);
}

static void test_media_and_input_changes_remove_rate_ownership(void)
{
    for (unsigned media = 0; media < 2; ++media) {
        struct fixture fixture;
        fixture_init(&fixture, NULL);
        vlc_addon_session_token *token = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(1));
        if (media)
            assert(vlc_addon_session_state_Media(fixture.state, true) == VLC_ADDON_SESSION_OK);
        else
            assert(vlc_addon_session_state_Input(fixture.state, false) == VLC_ADDON_SESSION_OK);
        assert(!snapshot(fixture.state).rate_owned);
        assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope,
            now + VLC_TICK_FROM_SEC(2)) == VLC_TICK_MAX);
        vlc_addon_session_token_Release(token);
        fixture_destroy(&fixture);
    }
}

static void test_counter_exhaustion_is_closed_until_rekey(void)
{
    const struct vlc_addon_session_limits limits = { 12, 4 };
    struct fixture fixture;
    fixture_init(&fixture, &limits);
    vlc_addon_session_token *token = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(1));
    unsigned attempts = 0;
    while (vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
        VLC_ADDON_SESSION_PAUSE) == VLC_ADDON_SESSION_OK)
        assert(++attempts < 12);
    assert(snapshot(fixture.state).exhausted);
    // A user rate still removes the old correction after counter exhaustion.
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
        VLC_ADDON_SESSION_USER_RATE) == VLC_ADDON_SESSION_EXHAUSTED);
    assert(!snapshot(fixture.state).rate_owned);
    assert(vlc_addon_session_state_Input(fixture.state, false) == VLC_ADDON_SESSION_EXHAUSTED);
    assert(vlc_addon_session_state_Input(fixture.state, true) == VLC_ADDON_SESSION_EXHAUSTED);
    struct vlc_addon_session_input_scope refused;
    assert(!vlc_addon_session_scope_Capture(fixture.state, &refused));
    assert(vlc_addon_session_state_Rekey(fixture.state, owner) == VLC_ADDON_SESSION_INVALID);
    assert(vlc_addon_session_state_Rekey(fixture.state, (vlc_addon_session_id) { 90, 91 }) == VLC_ADDON_SESSION_OK);
    assert(!snapshot(fixture.state).exhausted);
    assert(!snapshot(fixture.state).input && !snapshot(fixture.state).media);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_pending_rate_drop_and_failed_apply_release_reservation(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    const struct vlc_addon_session_action action = correction(1.02f, VLC_TICK_FROM_SEC(1));
    vlc_addon_session_token *first = admit(&fixture, 1, action);
    vlc_addon_session_token *second = admit(&fixture, 2, action);
    vlc_addon_session_token_Hold(first);
    vlc_addon_session_token_Release(first);
    assert(snapshot(fixture.state).retained_tokens == 2);
    vlc_addon_session_token_Release(first);
    assert(snapshot(fixture.state).rate_owned);
    assert(vlc_addon_session_token_Begin(second, &fixture.scope, &action, now));
    vlc_addon_session_token_Complete(second, false);
    assert(!snapshot(fixture.state).rate_owned);
    vlc_addon_session_token_Release(second);
    fixture_destroy(&fixture);
}

static void test_rate_limit_boundaries_and_other_effective_rate(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    struct vlc_addon_session_command request = command(&fixture, 1,
        correction(0.95f, VLC_ADDON_SESSION_MAX_CORRECTION));
    vlc_addon_session_token *token = NULL;
    fixture.caps.effective_rate = 1.2f;
    assert(vlc_addon_session_state_Admit(fixture.state, &request, &fixture.caps, now,
        &token) == VLC_ADDON_SESSION_DENIED);
    fixture.caps.effective_rate = 1;
    assert(vlc_addon_session_state_Admit(fixture.state, &request, &fixture.caps, now,
        &token) == VLC_ADDON_SESSION_OK);
    vlc_addon_session_token_Release(token);
    token = admit(&fixture, 2, correction(1.05f, VLC_TICK_FROM_MS(1)));
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_rate_lease_expiry_and_local_restore_order(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    struct vlc_addon_session_snapshot current = snapshot(fixture.state);
    assert(vlc_addon_session_state_Grant(fixture.state, &current.stamp, lease,
        VLC_ADDON_SESSION_REMOTE_OPERATIONS, now, VLC_TICK_FROM_SEC(1)) == VLC_ADDON_SESSION_OK);
    vlc_addon_session_token *token = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(3));
    assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope, now) == now + VLC_TICK_FROM_SEC(1));
    float base = 0;
    assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope,
        now + VLC_TICK_FROM_SEC(1), &base));
    // The input claimed restore first. A later user command follows it in the
    // same input. Its ownership record prevents any further restore attempt.
    assert(vlc_addon_session_state_Record(fixture.state, VLC_ADDON_SESSION_LOCAL,
        VLC_ADDON_SESSION_USER_RATE) == VLC_ADDON_SESSION_OK);
    vlc_addon_session_token_CompleteRateRestore(token, true, now + VLC_TICK_FROM_SEC(1));
    assert(!snapshot(fixture.state).rate_owned);
    assert(vlc_addon_session_token_NextDeadline(token, &fixture.scope, now) == VLC_TICK_MAX);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_rekey_waits_for_applied_rate_restore(void)
{
    struct fixture fixture;
    fixture_init(&fixture, NULL);
    vlc_addon_session_token *token = apply_rate(&fixture, 1, 1.02f, VLC_TICK_FROM_SEC(1));
    const vlc_addon_session_id next_owner = { 1000, 2000 };
    assert(vlc_addon_session_state_Rekey(fixture.state, next_owner) == VLC_ADDON_SESSION_BUSY);
    assert(vlc_addon_session_state_Revoke(fixture.state, lease));
    float base = 0;
    assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, now, &base));
    assert(vlc_addon_session_state_Rekey(fixture.state, next_owner) == VLC_ADDON_SESSION_BUSY);
    vlc_addon_session_token_CompleteRateRestore(token, true, now);
    assert(vlc_addon_session_state_Rekey(fixture.state, next_owner) == VLC_ADDON_SESSION_OK);
    vlc_addon_session_token_Release(token);
    fixture_destroy(&fixture);
}

static void test_adjusted_or_partial_rate_keeps_restore_guard(void)
{
    for (unsigned success = 0; success < 2; ++success) {
        struct fixture fixture;
        fixture_init(&fixture, NULL);
        const struct vlc_addon_session_action action = correction(1.02f, VLC_TICK_FROM_SEC(1));
        vlc_addon_session_token *token = admit(&fixture, 1, action);
        assert(vlc_addon_session_token_Begin(token, &fixture.scope, &action, now));
        vlc_addon_session_token_CompleteRate(token, success, 1.01f);
        assert(snapshot(fixture.state).corrected_rate == 1.01f);
        assert(snapshot(fixture.state).rate_owned);
        const vlc_tick_t restore = success ? now + VLC_TICK_FROM_SEC(1) : now;
        float base = 0;
        assert(vlc_addon_session_token_BeginRateRestore(token, &fixture.scope, restore, &base));
        assert(base == 1);
        vlc_addon_session_token_CompleteRateRestore(token, true, restore);
        vlc_addon_session_token_Release(token);
        fixture_destroy(&fixture);
    }
}

int main(void)
{
#define RUN(name) do { name(); puts(#name " passed"); } while (0)
    RUN(test_admission_uses_exact_stamp);
    RUN(test_payload_is_exact_and_one_use);
    RUN(test_local_priority_before_or_after_begin);
    RUN(test_revoke_and_owner_close);
    RUN(test_later_admission_preserves_fifo_order);
    RUN(test_resume_requires_current_authority);
    RUN(test_scope_reuse_and_foreign_owner);
    RUN(test_core_origin_stays_separate);
    RUN(test_closed_capabilities_and_parameters);
    RUN(test_lease_bounds_and_renewal);
    RUN(test_rate_restore_after_end_or_seek);
    RUN(test_rate_restore_uses_no_spare_token);
    RUN(test_user_rate_wins_before_or_after_apply);
    RUN(test_seek_during_correction_apply_requests_restore);
    RUN(test_rate_updates_keep_original_base_and_failure_state);
    RUN(test_restore_failure_has_bounded_retry);
    RUN(test_old_rate_tokens_cannot_touch_new_owner);
    RUN(test_media_and_input_changes_remove_rate_ownership);
    RUN(test_counter_exhaustion_is_closed_until_rekey);
    RUN(test_pending_rate_drop_and_failed_apply_release_reservation);
    RUN(test_rate_limit_boundaries_and_other_effective_rate);
    RUN(test_rate_lease_expiry_and_local_restore_order);
    RUN(test_rekey_waits_for_applied_rate_restore);
    RUN(test_adjusted_or_partial_rate_keeps_restore_guard);
    return 0;
}
