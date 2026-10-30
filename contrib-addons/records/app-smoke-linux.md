# Linux full app smoke test

Step 1 of the [continuation plan](../07-continuation-plan.md), first part.
Written 30 October 2026.

## What runs

[`test/addon_app_smoke.py`](../../test/addon_app_smoke.py) starts the full app
(`vlc -I qt`) with the offscreen Qt platform and a temporary home, so it never
reads or changes the user's profile. A `cli` control socket drives it:

1. Start, and wait for the control socket.
2. Wait until the seven bundled add-ons are in the private profile.
3. Play `test/samples/meta.mp3` to the PLAYING state, then stop it.
4. Measure idle CPU time for 30 seconds, and resident memory, over the app
   and every child process.
5. Quit through the normal interface shutdown. The exit status must be 0.

`--trace` counts process launches with strace. `--windows 2` starts a second
app process on the same profile, then quits both in the order given.

meson registers `addon_app_smoke` and `addon_app_smoke_two_windows` with the
`app` label. They run serially:

```sh
source .design-reference/native-build/env.sh
meson test -C build-redesign --suite app
```

The script sets `VLC_PLUGIN_PATH`, `VLC_DATA_PATH` and `VLC_LIBEXEC_PATH` to
the build tree. Without them, the build's `vlc` loads plug-ins from elsewhere,
and the Qt interface cannot start `vlc-qt-check`. That spawn failure is
silent.

## Results

Commit `49056a7d5a`, Linux (WSL2), meson debug build, Mesa llvmpipe.

| Measure | One window | Two windows, first quits first | Two windows, second quits first |
| --- | ---: | ---: | ---: |
| Start to control socket | 0.70 s | 0.70 s | 0.70 s |
| Seven bundled add-ons ready | 1.56 s | 1.57 s | 1.56 s |
| Idle CPU, 30 s | 0.19 s | 0.22 s | 0.18 s |
| Processes alive at idle | 1 | 1 | 1 |
| Resident memory | 280 MB | 283 MB | 282 MB |
| Second window start | — | 0.40 s | 0.35 s |
| Quit (in quit order) | 0.16 s | 0.16 s, 0.77 s | 0.82 s, 0.16 s |

With strace, the app starts two processes: `vlc-qt-check` and
`vlc-preparser`. No add-on worker starts during startup, bundled
preparation or idle. strace raises the idle CPU figure (1.03 s), so the
idle numbers above come from the runs without it.

There is no Linux pre-add-on baseline. The retained baseline in the
[performance contract](../performance-contract.md) is a Windows build. These
numbers are the first Linux record, not a comparison.

## Defects found

1. **Startup crash with the offscreen platform** (fixed in `818c1032a7`).
   Qt 6.8's offscreen platform provides the X11 native interface with a null
   XCB connection. `CompositorX11::init` accepted it, opened its own
   connection through `DISPLAY`, and the render window then called
   `xcb_flush()` on Qt's null connection. The kernel log showed the fault at
   `xcb_flush+0xc`. The compositor now also requires Qt's connection. This
   code is unchanged from upstream.
2. **Quit crash in the WSL D3D12 driver** (open, not in app code as far as
   the evidence shows). With the GPU-backed Mesa D3D12 driver, quit crashed
   in both runs. The faulting instruction was in
   `/usr/lib/wsl/lib/libd3d12core.so`, on the Qt UI thread after the Qt
   event loop ended, at an address that was no longer mapped. With
   `LIBGL_ALWAYS_SOFTWARE=1` (llvmpipe), quit exited with status 0 in all
   runs. The smoke test uses llvmpipe. A debugger was not available, so the
   unloading path is not proven. Check a pre-add-on Linux build on the same
   driver before treating this as a driver defect.

## Every label in the VLC build

This was the first time meson ran the labelled add-on tests in the VLC build
(`build-redesign`), not in their standalone harnesses. The first run had 9
failing tests and 1 timeout. The causes and fixes:

| Cause | Tests | Fix |
| --- | --- | --- |
| `meson test --no-rebuild` after building only the test executables skipped their fixture processes | prepared invocation, metadata attempt, artwork aggregate | None needed. Let meson build test dependencies. |
| No plug-in path: the build tree has no default LibVLC plug-in directory | stream proxy, discovery source owner, playable gate | `VLC_PLUGIN_PATH` in the add-on test environment (`e147165d27`) |
| The VLC build defines `_FILE_OFFSET_BITS=64`, so `open()` becomes `open64()` and `--wrap=open` does not intercept it | display catalogue, metadata catalogue | Wrap `open64` too (`e147165d27`) |
| A 200 ms expiry window. With `MALLOC_PERTURB_`, which meson sets, preparation was slower, and the gate correctly reported an expired stream | playable gate | 2-second expiry, wait until it passes (`e147165d27`) |
| **Core regression:** thumbnail requests started as successful, so a thumbnail without a picture reported success with a NULL picture | `test_src_input_addon_source_playback` | `8b0fd6cb6e` |
| A stale test: `fetcher_shutdown.c` missed the probe outcome argument and did not compile | `test_src_preparser_fetcher_shutdown` (not an add-on label) | `12940f143c` |
| Suites near the 30-second default timeout under parallel load | runtime, profile writes, playable gate | 90-second timeout (`e147165d27`, `375f9dba89`) |

The thumbnail regression came from `9765ab54ac`. Its test failed from that
commit on, but the test was not run again in a full build. The upstream
thumbnail tests (`test_src_preparser_thumbnail`,
`test_libvlc_media_thumbnail_*`) pass with the fix.

After the fixes, at `375f9dba89`, Linux, meson debug build:

| Label | Tests | Cases | Failed | Skipped |
| --- | ---: | ---: | ---: | ---: |
| `profile` | 21 | 720 | 0 | 0 |
| `metadata` | 50 | 727 | 0 | 0 |
| `source` | 22 | 289 | 0 | 1 |
| `ui` | 7 | 170 | 0 | 0 |
| `session` | 4 | 28 | 0 | 0 |
| `visualizer` | 12 | 116 | 0 | 0 |
| `app` | 2 | — | 0 | 0 |

The skipped case is
`TestAddonDiscoveryService::attachedRowsAndConstructorsAreMemoryOnlyAndRetainViewsOnUnrelatedChanges`.
It needs a GNU link wrapper that only its standalone harness supplies. The
`app` tests are scripts and report no QtTest cases. The 18 tests that only
the standalone CMake project registers did not run.

## Build environment

`build-redesign` needs `.design-reference/native-build/build-env.sh` for a
full build. It sources `env.sh` and adds the include and library paths that
some dependency `.pc` files and Debian multiarch headers do not supply. The
`libmysofa-dev` and `libspeex-dev` headers from Ubuntu 22.04 are unpacked in
`.design-reference/native-build/extra-dev/`, because earlier space cleanup
removed the copies that the build used. projectM is disabled in this local
build: its library was removed, and no matching package exists.

## Add-on test driver

Decided on 30 October 2026 by the user: the driver is a build option that is
off by default. `meson configure <build> -Daddon-test-driver=true` builds
`AddonTestDriver` (`modules/gui/qt/addons/addon_test_driver.*`) into the Qt
module (`7ddc4b28ee`). It starts only when `VLC_ADDON_TEST_DRIVER_SOCKET`
names a socket path. With the option off, the module has no driver symbols
and no reference to that variable, and meson does not register the driver
test. `build-redesign` has the option on.

The driver answers one line command at a time with one JSON line:

| Command | Action |
| --- | --- |
| `extensions` | Refresh the Extensions package list and wait until it is ready |
| `install <directory>` | Install a local package with its required grants, and enable it |
| `discover <package> <query>` | Open the package's discovery source, load its roots, search, go Back, search again |

`addon_app_smoke.py --driver` (meson test `addon_app_smoke_driver`) runs
these with the synthetic `discovery-fixture` package. That package has no
network, credential or file access, so the run needs no network exception.

Results at `7ddc4b28ee`, same platform as above, 30 s idle:

| Measure | Value |
| --- | ---: |
| Extensions list ready, 7 rows | 18 ms |
| Install and enable the fixture | 61 ms |
| Worker start and first call (roots) | 144 ms |
| Warm call (search) | 3–4 ms |
| Second open of the source (roots) | 4 ms |
| Worker processes started (strace) | 1 for both opens |
| Workers after use and at idle | 0 |
| Idle CPU after add-on use, 30 s | 0.15 s |
| Resident memory | 294 MB |

The discovery worker is reused across views and stops when no display
holds the source. These are the long-lived discovery worker numbers. The
prepared metadata path still starts a new process and compiles the module
for each call (plan F1); this run does not measure it.

Two findings came from this run:

1. **Discovery rows disappeared after Back or a repeated search** (fixed in
   `74d63a50da`). The coordinator reported rows the view had already shown
   as updates, and the service cleared them for a new page. A repeated
   search of the fixture showed no rows. Two regression tests cover Back,
   a repeated search and Load more.
2. **Installation refuses a busy store.** `installLocalPackage` takes the
   store lock with `tryLock(0)`. While the bundled add-ons are prepared at
   startup, or while another owner holds the lock, installation fails with
   "The add-on store is busy." This is by design, but a user who installs
   during startup sees it. The driver waits and retries. Consider a queued
   install in the Extensions UI.

## Open for Step 1

- Radio Browser playback in the app. No local Radio Browser fixture exists:
  its probe uses the live public service, and the public HTTPS policy
  refuses a loopback fixture. Either add a live, opt-in smoke step or a
  test-build network exception behind the same option.
- A per-call measurement of prepared metadata invocations (F1), before and
  after the precompiled module change in Step 3.
- The WSL D3D12 quit crash (above).
