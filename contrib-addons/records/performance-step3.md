# Performance fixes (Step 3)

Step 3 of the [continuation plan](../07-continuation-plan.md). Written
30 October 2026. Linux (WSL2), meson debug build, Mesa llvmpipe.

## Changes

| Finding | Commit | Change |
| --- | --- | --- |
| F2, 25 ms cancellation poll | `e62f9813f8` | Prepared invocations retire on events: the joined-work cancellation and the invocation's three store tokens signal a native wake. No timer per invocation. |
| F4, global busy cursor on Windows | `48e8bee05f` | Every add-on helper launch goes through `prepareHelperLaunch()`, which counts it and sets `STARTF_FORCEOFFFEEDBACK` on Windows. `vlc_spawn` and the plugin cache scan set the same flag. |
| F1, compile per worker start | `6efa39908f` | The worker loads host-made precompiled code when its SHA-256 matches, else compiles from source. The cache is keyed by release digest and worker build. |
| F3, unlimited crash restarts | `097c2d5fd5` | A crash ledger per package: waits of 2 s and 4 s, then the package is turned off through the guarded writer on the third crash in five minutes. |

The Extensions page shows F3's ledger on an enabled row ("Enabled · Stopped
unexpectedly · 2 time(s)"), read from `AddonManager::health()`; covered by
`tst_AddonInstaller`. That QML runner had not linked since `b73aa857e5`
because neither build linked the add-on host library into `qml_test`; both
do now. The Windows cursor result needs the live check on Windows (Step 5).

## Measurements

**Worker start to ready**, median of 7:

| Module | From source | Precompiled |
| --- | ---: | ---: |
| discovery-fixture (96 KB) | 133 ms | 1.6 ms |
| radio-browser, thesubtitledb, tvmaze (150–155 KB) | 241–246 ms | 2.2–2.4 ms |
| musicbrainz, lastfm, french-tv, google-images (199 KB) | 297–300 ms | 2.5–2.6 ms |

A wrong digest falls back to source.

**Repeated lookup cost**: a complete prepared invocation (process start, one
call, clean exit) takes 2.1 ms median, 2.4 ms p90, with the precompiled
module; 140 ms from source. A call on a warm process takes 0.07 ms. In the
app, the discovery worker's first open takes 144–148 ms (first start, before
its precompile) and later calls 3–4 ms.

**Warm worker (F1, second part): not built.** The precompiled module removed
almost all of the per-call cost. A warm process would save about 2 ms more
per lookup but replace the process-exit proof with a weaker one. See the
revised decision in the plan.

**Helper launches**: the driver smoke run starts 2 helpers in total (the
discovery worker and its one-time precompile), and none while the app is
idle. The smoke test fails if a helper starts while idle.

**Against the pre-add-on baseline.** The baseline is source `619b57dd85`,
built on Linux in a separate worktree (`../vlc-baseline/build`) with the
same options. It needed the upstream offscreen compositor fix (`818c1032a7`)
applied locally to start headless. It crashes in its own
`preparserserializer_json` plugin when it plays the sample, so both builds
were compared without playback: start, 30 s idle, quit; five interleaved
runs each.

| Measure | Baseline | Current |
| --- | ---: | ---: |
| Idle CPU, 30 s, median (range) | 0.40 s (0.26–0.57) | 0.15 s (0.14–0.29) |
| Resident memory | 227 MB | 258 MB |
| Start to the control socket | 0.30 s | 0.65 s |
| Quit | 0.11 s | 0.16 s |
| Processes at idle | 1 | 1 |

Idle CPU is within the baseline's range and lower in four of five runs.
Memory is 31 MB (14%) higher.

**Startup is 0.35 s slower on this machine.** Part of it comes from this
machine's layout: libraries are resolved through `LD_LIBRARY_PATH` entries
that are symlinks to the Windows drive, at about 1.5 ms per lookup, and the
current build links more libraries. A stale plugin cache after rebuilds
added another 0.27 s until it was regenerated (`vlc-cache-gen`). The rest is
not yet attributed. Measure startup on a normal Linux install before closing
this item.

Basic UI interaction timings are not measured: there is no harness that
drives the UI.

## Step 3 exit

- Idle CPU within the pre-add-on baseline: **met** on Linux.
- Repeated-lookup cost reported: **met**.
- Basic UI timings: **open** (no UI-driving harness).
- Startup: **open**, 0.35 s slower here, partly explained.
