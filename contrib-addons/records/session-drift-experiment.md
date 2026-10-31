# Two-player drift experiment (Step 7)

Step 7 of the [continuation plan](../07-continuation-plan.md). Written 31
October 2026. Implements the prototype that
[visualizers and sessions](../05-visualizers-and-sessions.md) asks for before
coordinated playback is claimed: two-player drift measurement, seek and
bounded-rate correction, with explicit output-latency uncertainty.

## Setup

`test_src_player_addon_session_drift` (label `session`) runs two real
`vlc_player` instances in one process. Both play the same mock media
(audio only, `adummy` output, monotonic clock master). Player B starts at
least 300 ms after A. The timeline is the last player timer point
interpolated to now (`vlc_player_timer_point_Interpolate`), which is what a
session observation uses. `vlc_player_GetTime` moves in steps of up to
1.4 s and is not usable for this.

B is then corrected through the native session API (`vlc_addon_player_Grant`
with a 60 s lease, then `vlc_addon_player_Apply`): a seek to A's current
time, and, for a residual over 20 ms, a temporary rate of 1.05 or 0.95 for
residual / 0.05, at most 5 s. Output latency of `adummy` is unknown and is
reported as unknown.

## Results (Linux, three runs, the same to 1 ms)

| Step | Mean offset of B | Spread | Drift |
| --- | --- | --- | --- |
| After join | 492 ms | 0 to 1412 ms (startup) | -130 ms/s while B starts |
| After seek | 48–49 ms | 0 ms | 0 ms/s |
| During rate 1.05 for 960–980 ms | 48–49 ms | — | — |
| After rate restore (2 s later) | 48 ms | 0 ms | 0 ms/s |

- The seek command brings B to within about 50 ms, and B then stays there.
  The residual is the seek latency, which a controller can measure and
  add to the target.
- The bounded rate is admitted, B reports 1.05 during the correction, and
  the rate returns to 1.00 at its deadline.
- The rate did **not** close the residual here. With the mock demuxer and
  the dummy output, each player's timeline follows the shared clock, not
  consumed samples. Rate convergence needs a real audio output.
- The rate restore can report one timer discontinuity (one sample of
  -188 ms in one run). The measurement waits 2 s after the restore.

The test asserts what this setup can show: the late join is visible, the
seek reduces it below 150 ms and stays without drift, the rate is active
during its duration and restored after it.

## Consequence for v1

Coordinated playback can claim seek-based alignment to about 50 ms on one
machine, plus bounded rate commands that are admitted and restored. Rate
convergence, output latency on real devices, and two machines with
network jitter are not measured. Run the same experiment with a real audio
output (PulseAudio, WASAPI) and two hosts before a stronger claim.
