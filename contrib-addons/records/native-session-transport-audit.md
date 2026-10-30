# Native session transport audit

This note defines the native boundary for a session playback adapter.
It records the player admission and input execution boundaries.
The native API is separate from the room protocol and the session interface.

## Private state implementation

The new [state code](../../src/player/addon_session_state.c) and
[private API](../../src/player/addon_session_state.h) define the native authority record.
The [state tests](../../test/src/player/addon_session_state.c) run without Qt.
The [native fixture](../../test/src/player/addon_session.c) uses a builtin audio demux
and the real player and input threads.
Its normal Core build and runtime result must pass before this mount is accepted.
The state tests alone do not prove local priority in the real input FIFO.

The state retains one owner identity, media generation, input generation, and lease.
An input scope holds the state after the session interface closes.
Admission issues a typed token with the exact operation and parameters.
The input calls `Begin` before it changes native playback state.
`Begin`, local intent, and lease revocation use the same short state mutex.
The mutex does not call the player, Qt, the input FIFO, or a callback.
State methods release the mutex before they return.
The caller then wakes the input queue.

The token has one execution attempt.
A wrong input scope or changed parameter also consumes that attempt.
Later admission does not invalidate an earlier command in the same FIFO.
Local input, a core interruption, lease replacement, and media or input changes do invalidate it.
A command that passed `Begin` can complete before a later local command.
The native mount must preserve that FIFO order.
The snapshot separates the last intended action from the last completed action.
The completion record includes its own stamp, lease, origin, and sequence.
A late completion cannot replace the current local intent.

The initial limits are 64 retained tokens, a 60-second lease, and a 5-second speed correction.
A correction must stay within five percent of the saved user rate and the input rate limits.
Lease renewal can extend the deadline without a new sequence or authority epoch.
It cannot resume a lease that local input suspended.
The host must perform the explicit resume action with a current snapshot.
Counter exhaustion denies remote commands.
The host must use a new owner identity and bind new input scopes before it grants authority again.
The host must restore an applied correction before it replaces that owner identity.

A temporary correction changes only the input rate.
It does not change the saved player rate for later media.
The input retains a rate guard and includes its deadline in the control wait.
Restore needs no new token or FIFO slot.
Session end and local seek retain this cleanup authority.
A later user rate choice, input removal, or player destruction removes it.
Session end uses `Revoke`; `Close` is for player destruction.
The rate result must use the observed input rate, because a demux can adjust the requested value.
Restore success means that the observed rate equals the saved base rate.
The generic `Control` return value reports a required update; it does not prove that result.
A failed restore waits at least 250 milliseconds before its next attempt.

The [public native API](../../include/vlc_addon_player.h) requires the player lock.
It accepts typed playback values; it does not accept a URL or variable name.
`Apply` success means that the native FIFO accepted the command.
An executed command sends its captured completion through a separate listener.
The listener runs under the player lock and must copy its value before it returns.
It must defer interface work and must not call the player.
The host can register at most eight completion listeners.
FIFO reduction, revocation, input removal, and owner shutdown can discard a queued command.
Those commands cannot execute and do not promise an execution callback.
The host must cancel their pending interface requests when the authority snapshot changes
or a newer command replaces them, with a bounded timeout as the final fallback.

Remote pause preserves the native buffering rule.
It returns a failed execution result while the output clocks are not ready.
It never leaves a deferred pause after its token completes.
If a native pause has a partial effect, the input removes that effect before it reports failure.
Automatic next-item maintenance does not claim local playback intent.
The public next-item choice still records local intent.

## Finding

A Qt signal cannot give local input priority over every delayed remote command.
A hotkey or native API can change the player before Qt receives the related event.
The remote command must compare its authority with native state under the player lock.

The player already has one lock for these public transport operations.
`vlc_player_get_input_locked()` asserts that lock.
The main public player header does not need a new layout or callback field.
Private state can stay in `src/player/player.h`.
A separate `include/vlc_addon_player.h` can expose a small native session API.

## Required private state

| Value | Purpose |
| --- | --- |
| Owner nonce | Reject commands from an old player instance |
| Media generation | Reject commands after an input or media change |
| Transport revision | Compare the command with all accepted transport actions |
| Local revision | Invalidate queued remote controls after local input |
| Active lease nonce | Bind remote authority to the current host-approved session |
| Last command sequence | Reject duplicate or reordered remote commands |
| Last command origin | Distinguish local, session, and automatic core actions |
| Rate ownership token | Restore a temporary rate only while the same input and correction retain authority |

Use counters and a random owner nonce, not input pointers or media URLs.
Counter exhaustion must revoke the lease and create a new owner identity.
The host assigns the session origin.
An add-on cannot supply a native origin or lease nonce.

A local transport action increments the native revision and suspends the active lease.
Do this before it queues an input control or returns from a valid repeated action.
For example, a local Pause action must suspend the lease even when playback is already paused.
No later Qt event is necessary to enforce this rule.

An accepted session action increments the transport revision.
It does not increment the local revision or suspend its own lease.
Automatic core actions have a separate explicit origin.
Do not use a mutable global or thread-local flag to mark session commands.
Such a flag can give a callback the wrong origin.

## Exact player hook locations

The line numbers below identify the audited source state.
Use the named function if later edits move the line.

| Source and line | Function | Required action |
| --- | --- | --- |
| `src/player/player.c:56` | `vlc_player_OpenNextMedia` | Change media generation before the media event; revoke old input authority |
| `src/player/player.c:918` | `vlc_player_SelectTitleIdx` | Record local timeline intent before the input control |
| `src/player/player.c:946` and `:958` | `vlc_player_SelectNextTitle`, `vlc_player_SelectPrevTitle` | Record local timeline intent |
| `src/player/player.c:1004` | `vlc_player_SelectChapterIdx` | Record intent before timer updates and the input control |
| `src/player/player.c:1017` and `:1030` | `vlc_player_SelectNextChapter`, `vlc_player_SelectPrevChapter` | Record local timeline intent |
| `src/player/player.c:1120` | `vlc_player_SetCurrentMedia` | Revoke immediately, including when input shutdown defers the switch |
| `src/player/player.c:1161` | `vlc_player_SetNextMedia` | Record an explicit local queue choice; do not wait for the media switch |
| `src/player/player.c:1263` | `vlc_player_Start` | Record origin before the repeated-start and deferred-start branches |
| `src/player/player.c:1310` | Input creation in `vlc_player_Start` | Change input generation even when the media item is unchanged |
| `src/player/player.c:1333` | `vlc_player_Stop` | Revoke before the empty-input branch; invalidate queued controls |
| `src/player/player.c:1372` | `vlc_player_SetPause` | Use an explicit origin in the private pause helper |
| `src/player/player.c:1400` and `:1410` | `vlc_player_NextVideoFrame`, `vlc_player_PreviousVideoFrame` | Suspend remote timeline control |
| `src/player/player.c:1451` | `vlc_player_ChangeRate` | Record origin before the saved rate or input control changes |
| `src/player/player.c:1547` and `:1557` | `vlc_player_SeekByPos`, `vlc_player_SeekByTime` | Validate remote authority before timer updates or input controls |
| `src/player/player.c:1594` | `vlc_player_SetAtoBLoop` | Suspend the session when the user changes repeat authority |
| `src/player/player.c:1657`, `:1690`, and `:1720` | A-to-B time, position, and reset functions | Record local repeat intent even when no immediate seek occurs |
| `src/player/player.c:1761` | `vlc_player_Navigate` | Suspend remote control before interactive menu navigation |
| `src/player/player.c:2055` | `vlc_player_CorkCallback` | Use automatic core origin and suspend remote control during interruption |
| `src/player/input.c:314` | Current input removal in the stopping-state handler | Invalidate the input generation before observers receive later state |
| `src/player/input.c:1260` | `vlc_player_input_New` | Assign a fresh input identity; do not infer identity from allocation addresses |

`SetTime`, `SetPosition`, their fast variants, and relative jump helpers call the two seek entry points.
Rate increment and decrement call `vlc_player_ChangeRate` through `vlc_player_ChangeRateOffset`.
Title and chapter pointer helpers call their index entry points.
One hook in each final entry point avoids duplicate revision changes.

Hotkeys already call these native player functions in `modules/control/hotkeys.c:255`.
The CLI, D-Bus, playlist, VLM, and LibVLC paths also call the native player API.
The common player hooks therefore cover these callers without Qt-specific heuristics.

## Automatic core actions

Some internal code calls the same public functions.
Convert those calls to private helpers with an explicit automatic origin.

| Source and line | Automatic action |
| --- | --- |
| `src/player/input.c:65` | A-to-B loop seeks |
| `src/player/input.c:742` | Saved title restoration |
| `src/player/input.c:757` | Saved position restoration after title selection |
| `src/player/input.c:1035` | End-of-file play-and-pause |
| `src/player/input.c:1042` | End-of-file repeat reset |
| `src/player/medialib.c:156` | Saved playback position |
| `src/player/medialib.c:169` | Saved playback rate |
| `src/player/player.c:314` | Deferred input start |
| `src/player/player.c:2072` and `:2078` | Audio cork pause and resume |

A core action that changes the timeline still changes the transport revision.
The session adapter must reject a conflicting A-to-B loop or wait until the user removes it.
Do not report a saved-state restoration as a remote command or a fresh user command.

## Input control queue boundary

Player admission and input execution are separate steps.
The input FIFO can retain a remote command after the player accepts it.
It must retain the native provenance and local revision with that command.

`src/input/input.c:1593` queues input controls under `lock_control`.
`ControlGetReducedIndexLocked()` at line 1547 can replace the last control of the same type.
`ControlPop()` at line 1629 removes controls in order.
It can delay a seek while the input prepares data.
`ControlPopEarly()` at line 1712 handles only program and track setup controls.
It does not execute playback state, rate, or seek controls early.

Add private provenance fields to the queued control representation.
Preserve them when the FIFO reduces consecutive commands.
Reject a queued remote command after a newer local revision or lease revocation.
Wake the input queue when revocation makes a delayed remote seek obsolete.
Do not acquire the player lock while the input holds `lock_control`.
That lock order can conflict with the player-to-input control path.

Use a reference-counted core token for the input-side validity test.
Its state must remain valid after the interface or session owner closes.
It must not call Qt or dereference the session adapter.
The native player lock remains the authority for admission, revision changes, and lease changes.

A command that has already entered the demux operation can precede a later local action.
The native contract must state that limit.
It must reject every remote command that has not entered execution when local authority changes.
The later local command must keep its normal input FIFO order.

## Separate API proposal

Expose native snapshot, lease, revoke, and apply functions in the new header.
Require the caller to hold the player lock, as the existing player API does.
The apply function receives a typed command and returns an explicit status.

The typed command contains the owner nonce, media generation, expected transport revision, lease nonce, and command sequence.
Its operation is a closed enum: play, pause, absolute seek, or bounded temporary rate.
The native code validates current capabilities and parameter limits before it queues a control.
No native session API accepts a media URL, local path, arbitrary variable name, or generic input control number.

Copy a snapshot while the player lock is held.
Release the lock before Qt serializes the snapshot or emits a signal.
The snapshot reports native origin and command sequence for echo suppression.
Asynchronous input events must not become new session commands.

Restore a temporary rate only if its native ownership token and input generation still match.
Track this rate token separately from the general transport revision.
A seek must not make a temporary rate permanent.
A user rate change must prevent a later restore from replacing that rate.
Session end, package withdrawal, permission removal, and owner shutdown revoke the core lease first.

## Required tests

Use a builtin PCM demux with the actual native player, player lock, and input FIFO.

| Case | Required result |
| --- | --- |
| Remote Qt task waits, then a native hotkey pauses | Reject the remote task at native admission |
| Remote seek waits in the input FIFO, then a local action arrives | Reject the queued remote seek |
| Local seek bypasses Qt through LibVLC or the playlist API | Suspend the native lease immediately |
| Local Pause repeats the current state | Suspend the lease |
| Stop and restart reuse the same media item | Reject the old input generation |
| Deferred media change waits for input shutdown | Reject old commands before shutdown completes |
| Remote command uses an old revision, lease, or sequence | Reject it without a timer or input mutation |
| Remote command succeeds | Report its native origin once; do not echo it as local input |
| User changes rate before a temporary restore | Preserve the user rate |
| Session owner closes with queued controls | Revoke controls without Qt callbacks or stale pointers |
| Automatic restore, loop, cork, or repeat runs | Preserve core behavior and use the correct origin |
| FIFO reduces consecutive controls | Preserve local precedence and provenance |

Also run the existing lifecycle, media, seek, pause, title, A-to-B, and discontinuity regression suites.
No working core feature needs removal for this boundary.

## Normal build evidence

The private state fixture passes 24 normal and sanitizer scenarios. The native player fixture passes all 12 scenarios in the normal Core build. It covers real input threads, queue reduction, local precedence, input replacement, bounded rate restore, buffering, pause, play, and native completion origin.

The player fixture uses `VLC_TEST_TIMEOUT=90` for the full suite. Every event wait has a five-second deadline. The registered Meson and Automake tests set this bounded suite alarm.

The shared-session transport, Qt host adapter, two-player drift checks, and the existing broad player regression suites remain open. The native state and player results do not prove these integrations.
