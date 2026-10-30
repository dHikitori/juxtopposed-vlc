# Session handoff

Written 30 October 2026 for continuing this work in the Claude Code desktop
app. Branch `addons`; last commit `5f1f99bc54`. The working tree was clean at
handoff.

## Start here

Read these in order:

1. [Continuation plan](07-continuation-plan.md). It has the findings, the
   decisions and the ordered steps. Its §5 is the work queue.
2. [Profile socket record](records/profile-socket.md). This is the step just
   finished.
3. [Completion backlog](completion-backlog.md). It lists the open
   requirement rows, with source references.
4. For Step 2: [profile authority coordination](profile-authority-coordination.md),
   [control protocol](profile-control-protocol.md) and
   [native peer readiness](records/profile-native-peer.md).

`README.md` and `01`–`06` are the design. `IMPLEMENTATION.md` is the
existing narrative status. Step 0b replaces it with a matrix.

## Decisions already made (30 October 2026)

1. **Several windows share one add-on profile**, with live cross-process
   revocation. This was the user's decision. The "first window owns add-ons"
   mode was refused.
2. **Worker reuse.** First precompile modules at install and measure. Then
   use a warm worker process with a fresh Wasm instance per invocation,
   using the receipt rules in plan F1.
3. **Evidence.** Use labelled test suites in the repository. New records
   cite test targets, not `/tmp` proof archives. Keep negative controls for
   security boundaries.
4. **Documents.** Move the records to `records/` in one commit that changes
   only paths and links. `IMPLEMENTATION.md` becomes a maturity matrix.

The user delegated decisions 2–4 to Claude.

## Where the work is

- **Step 0 (socket) is done** in `5f1f99bc54`. Linux normal 41/41 and
  ASAN/UBSAN 41/41 socket cases pass, plus 16/16 peer cases in each build.
  Native Windows passes 32/32 socket and 16/16 peer cases. Four negative
  controls fail as intended. Note 3 (the handshake deadline) moves to the
  Step 2 coordinator owner.
- **Next:** Step 0b (test labels, a check that the three build systems list
  the same tests, and the document move), and Step 1 (full Linux app build
  plus a headless smoke test). Step 1 does not depend on Step 2, so they can
  run in parallel.
- **Then:** Step 2 (the coordinator and client owners, the
  connection-bound write permit, write refusal, coordinator handover, the
  app mount and the crash matrix).
- **Do not** activate the native metadata owner before Step 2 passes.

## Environment notes

These matter on this machine (WSL2 on Windows):

- **Docker.** The shell profile sets `DOCKER_HOST` to a Podman socket. The
  Podman machine is stopped and does not have the images. The Windows build
  image `registry.videolan.org/vlc-debian-win64-posix:20260611225331` and
  the work volume `vlc-win-c2dba9113343-4ca4eb0c3e3d-ae87ffa6-work` are in
  Docker Desktop at `/var/run/docker.sock`. Run `unset DOCKER_HOST` before
  any Docker command, including the two cross-user socket tests. Otherwise
  those tests skip.
- **Disk reserve.** Earlier sessions keep at least 7.5 GB free. The
  component harness scripts stop a build below about 7.52 GB. With user
  approval, this session deleted 1.26 GB of old `/tmp/hakone-*` build trees
  and output archives, and kept the result files. About 7.8 GB was free at
  handoff. Ask before deleting anything else.
- **Ignored directory.** `.gitignore` matches `contrib-*`. Add new files in
  this directory with `git add -f`.
- **Component harness for the profile units:** `/tmp/hakone-profile-socket/`.
  - It builds a copy of the sources under `source/`. Copy the changed repo
    files there first.
  - Linux: `python3 run.py normal|san build`, then
    `python3 run.py <mode> test_addon_store_profile_socket` (or `..._peer`).
    It uses the Qt 6.8.3 SDK and ninja under `.design-reference/`.
  - Windows build: `python3 build-win.py` (Docker, see above). It uses
    `win-project/CMakeLists.txt`, which now has
    `qt_import_plugins(... NO_DEFAULT)`.
  - Windows run: copy `win/*.exe` and `run-windows.ps1` to
    `C:\Users\david\AppData\Local\Temp\hakone-profile-socket\`, then run
    `powershell.exe -NoProfile -ExecutionPolicy Bypass -File <that folder>\run-windows.ps1`.
  - Negative controls: `python3 negative-controls.py`. It refuses existing
    control directories, so move old ones aside first. The earlier ones are
    in `superseded-controls/`.
  - Step 0b replaces this harness with labelled in-repository test suites.
- **Full app builds:** `build-redesign/` (Linux, ninja) and `build-windows/`.
  Neither has been rebuilt with the latest owners. That is Step 1.
- **Commit trailer:** `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Open items owned by the user

- Manual Windows GUI checks of the current review package (source
  `74ae4267fc`; it does not include the profile or socket work).
- The Windows cursor report (`records/windows-cursor-feedback.md`). Plan F4 gives
  the hypothesis (global startup-feedback cursor from repeated helper
  launches) and the live check to run.

## Rules to keep

- Do not enable native metadata owner activation or provider cutover early.
- Do not widen the public HTTPS profile, or add LAN, plain HTTP or WebRTC
  scope.
- Do not change the user's app profile or the preserved pre-add-on build.
- Idle features add no timers or polling. Basic UI does no package I/O.
  Measure against `performance-contract.md`.
