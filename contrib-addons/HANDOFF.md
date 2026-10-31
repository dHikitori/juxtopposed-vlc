# Session handoff

Updated 31 October 2026. Branch `addons`; local commits only, nothing
pushed. The continuation plan (`07-continuation-plan.md` §5, Steps 0b–7) is
done except macOS, which needs a macOS host.

## Start here

1. [Requirements comparison](records/v1-requirements-comparison.md): what
   is open against `01`–`05`, by section.
2. [Maturity matrix](IMPLEMENTATION.md): evidence for every row.
3. [Windows review](records/windows-review-step5.md): the user's live
   rounds and the fixes after each one.

## Where the work is

- **Steps 0b–5** are done; see the plan and the matrix.
- **Step 6** rows 1–6 are mounted: identity choice, offline self-test and
  health period, result retention, device-code accounts with picked paths
  and session settings, read-only guest projections, discovery change feeds
  and queue filling. Records are in `records/` (one per row).
- **Step 7**: normative schemas with a shared host/schema corpus, the
  package validator, preview tool, templates and generated Rust types
  (`records/schemas-and-author-tools.md`); the drift experiment
  (`records/session-drift-experiment.md`); the text audit and the
  requirements comparison.
- **Windows:** a package with every change up to the end of Step 7 is built
  for the user's final check. The native metadata owner stays off by
  default until that check passes.
- **Player panels:** add-ons can own a player panel and its layout button
  ([record](records/player-panels-design.md), [guide](sdk/player-panels.md)).
  The Episodes panel is now the bundled package `org.videolan.episodes`,
  on by default; the built-in panel is removed. Waiting for the user's
  Windows check.
- **Tool environment:** the schema corpus check needs jsonschema 4.23.0
  (`python3 -m venv` in the scratchpad, then `pip install
  jsonschema==4.23.0`). Under the system jsonschema 3.2 the meson test
  skips.

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
  - `build-redesign` is a meson tree. Source
    `.design-reference/native-build/env.sh` first. Without it, the Qt tools
    (`rcc`, `qmltyperegistrar`) cannot load ICU 73 from the sysroot, and
    meson regeneration fails.
  - The Qt SDK, the sysroot, `build-redesign/test` and
    `build-redesign/modules/libqt_plugin.so.p` are symlinks to the C: drive
    (87 GB free), so test executables and Qt plugin objects do not use the
    WSL disk.
- **Commit trailer:** `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Open items owned by the user

- The final Windows check of the package built at the end of Step 7,
  now with player panels and the Episodes add-on.
- The decision to turn the native metadata owner on by default after it.
- Optional clean-up: the local branch `backup/addons-before-target-removal`
  and `refs/original` from the history rewrite that removed a committed
  Cargo `target` folder. Ask before deleting them.

## Rules to keep

- Do not enable native metadata owner activation or provider cutover early.
- Do not widen the public HTTPS profile, or add LAN, plain HTTP or WebRTC
  scope.
- Do not change the user's app profile or the preserved pre-add-on build.
- Idle features add no timers or polling. Basic UI does no package I/O.
  Measure against `performance-contract.md`.
