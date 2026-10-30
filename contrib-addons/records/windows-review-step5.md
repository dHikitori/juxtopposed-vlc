# Windows review package (Step 5)

Step 5 of the [continuation plan](../07-continuation-plan.md). Built
30 October 2026 into `build-windows/vlc-win64`. Its `portable/` settings
were kept. Nothing was released or pushed.

## Package

| Part | Source | SHA-256 |
| --- | --- | --- |
| Core and plugins | `8b78e46210` (Step 4) | `vlc.exe` `cd5d5473df08e6c6ba231e80863e2bc74e4518fb91b25041403f27b52260352c` |
| Qt plugin | `62bfe1513e` (launch logging) | `plugins/gui/libqt_plugin.dll` `5e4bbd8e9bef9561e78eaf201e61803236fbdcc88e150d2f74da04c76086d7d4` |
| Add-on worker | `8b78e46210`, Rust 1.99 host stable, `x86_64-pc-windows-gnu` | `vlc-addon-worker.exe` `daf92838d58ba4d8a4a466898f0bf6354413bfe6871106696002e0d100a1f35a` |

Build: `build-windows/build.sh make`, then `qt`, two jobs, contrib
`4ca4eb0c3e`, with `VLC_WINDOWS_RUST_TOOLCHAIN_DIR` set to the host stable
toolchain. The `addon_worker` block in `BUILD-INFO.json` still names the
previous verified worker bundle; this package's worker was built in the
container from current source.

## First user run: profile never joined (fixed)

The user's first run (package `62bfe1513e`) showed, for every bundled
add-on, "Other app windows did not release the add-on profile in time". The
Extensions list never loaded, "Temporarily disable add-ons" hung for about
30 s and then failed, and closing took 5–10 s. Only one window was open.

Cause: the package runs from `build-windows/vlc-win64`, so its `portable/`
profile is on the WSL share (`\\wsl.localhost\...`). A native probe showed
that `LockFileEx` fails there with `ERROR_INVALID_FUNCTION`; on C: it works.
The coordinator's fence needs that lock, so the profile session never
joined, and each add-on write waited its 10 s limit on the GUI thread.

Fix (`494b99aa88`): the fence reports `Unsupported` for that error, and the
profile host probes the drive before it shares the profile. Without locks it
keeps the profile unshared: ordinary store writes, serialized by
`store.lock`, and a log warning that other windows are not told about
changes.

Native Windows runs of the rebuilt tests (built in the Windows build
container, run from `%TEMP%`):

| Test | Result |
| --- | --- |
| `test_qt_addon_profile_host`, profile on C: | 5 of 5 pass |
| `test_qt_addon_profile_host`, profile on the WSL share, before the fix | 3 of 3 profile cases fail (`host->ready()` stays false) |
| `test_qt_addon_profile_host` with the fix, including `aDriveWithoutLocksLeavesTheProfileUnshared` on the WSL share | 6 of 6 pass |
| `test_qt_addon_store_profile_session` (handover and crash matrix) | 14 of 14 pass |

These are the first Windows runs of the Step 2 session suites.

The package is now `494b99aa88` (core and Qt): `vlc.exe`
`0bd7b6b8b5e263be92e6a3cc137a6a6800b9a61706836828ffdc99a386a84110`,
`libqt_plugin.dll`
`b1edbd0fd75a513e648d1b1f461cc03b7db37ce411b18f4dccec4358f262ce28`.

Still open: a write that cannot join a shared profile for another reason
still waits up to 10 s on the GUI thread. Moving these writes to joined work
with an asynchronous result is the follow-up named in
[the mount record](profile-mount.md).

## Second user run (package `494b99aa88`)

The Extensions list loaded and add-ons could be turned on and off. The user
reported these problems; the fixes are in the next package.

| Report | Cause | Fix |
| --- | --- | --- |
| TheSubtitleDB settings: "The required settings list is not correct" | Qt 6.8 gives JavaScript a list inside a C++ map as a sequence; `Array.isArray` is false, so every schema with `required` failed. Add-on views and themes had the same check. | `62f92f6bad`: settings, views and themes validate JSON copies of host data |
| Other add-ons open an empty settings dialog | They have no settings, but Settings was always offered | `62f92f6bad`: rows report `hasSettings`; Settings is hidden or disabled without it |
| Theme pack row: different help icon, Reset always shown | Its own help button; Reset also showed while the dropdown kept focus after a click | `b8aaeb420c`: the standard row's help button; Reset on hover or keyboard focus only |
| Use automatic artwork refreshes the whole library | The per-item save ended in a global clear and model reset | `b33227bbc2`: forget only that item and its folder collage |
| Library artwork reloads when scrolling back, and stutters | Full-size decode, no tile buffer, and the whole result map copied and every tile binding re-run per result | `b33227bbc2`: tile-size decode, a screen of buffered tiles, one update per frame, watcher set |
| Network > Radio says "This source has no media" first | The network model ended loading as soon as discovery started | `e4f8923a76`: loading ends with the first item, a failure or 5 s; the empty text waits for it |
| Radio Browser (add-on) hangs while it opens | Each invoke verified the whole package again, and every message re-read `state.json`; costly with the profile on the WSL share | `5175de237e`: reuse under the store's memory token; the view starts as loading |

The next package is `5175de237e` (`vlc.exe`
`ad71319384b920be392fdcf6efac3e4ee2d31c99b66cb26c7f3bb6eada454680`,
`libqt_plugin.dll`
`7bd30169e6afea2802764fca4daa3c291bb819361c177a27d8b0d1f8e2da1b37`). Its
build info says dirty only because this record was not yet committed. It
also has the Step 6.1 series choice.

Not yet explained, and need a check on Windows:

- **The window stutters while it is dragged during playback.** The most
  likely cause is background seek-preview sampling (`qt-seek-preview`, on by
  default): for the first minute of each video it decodes a frame every
  0.6–2 s. Check: turn off Settings › Playback › Seeking › "Show video
  previews on the seekbar" and drag again.
- **Close takes 5–10 s.** On Linux quit takes 0.16 s. The profile on the
  WSL share is the first suspect. Check: copy the whole `vlc-win64` folder,
  with its `portable/` settings, to a local drive and close it there.

## Third user run: freeze at video start, slow close

- The interface froze while a video started (about 10 s; much longer for a
  file on a network share), while playback went on. Seek previews were
  not the cause. When the sibling scan finished, the series view removed and
  re-added a file watch for every scanned folder on the GUI thread, which
  opens each folder over the network, and the episode code re-parsed every
  sibling's name with regular expressions built per call. Fixed in
  `a3bac38e82`: watches on their own thread with diffs, patterns compiled
  once, parsed identities cached.
- Close took 5 s (WSL folder) to 10–15 s (local copy). The close button
  quits without hiding the window, so every shutdown wait showed as a frozen
  window. Fixed in `fe749724e8`: windows hide when the quit starts, and a
  precompile job stops at once when cancelled (it could hold the drain for
  30 s, and a copied folder recompiles every add-on).
- `ddd73694e8` adds `--qt-hang-report` (off by default): when the
  interface thread stops for 0.5 s, its call stack is sampled to
  `%TEMP%\vlc-hang-report.txt` as module+offset frames, which resolve
  against the unstripped DLLs in the Windows build tree.

Package `575db46083`: `vlc.exe`
`967487f981675d8d802815d320d4c3a3d35a62569a33959ca4aa0cf7d8970d11`,
`libqt_plugin.dll`
`fe0160f6fd9fd7e4812d872107cb2b8a2857891aaf48bd861c105af07317453f`.

The user confirmed on Windows with package `575db46083` that starting a
video and closing the app both now work well. The other rows are still
being checked.

## Checks run here

The packaged worker ran natively on Windows with the bundled MusicBrainz
module (times include about 0.3 s of WSL launch overhead):

| Check | Result |
| --- | --- |
| `--precompile` | Exit 0 in 1.09 s; 808 KB compiled module |
| `--precompiled` with the right digest | Ready, `precompiled: true`, 0.32 s |
| From source | Ready, 0.62 s |
| `--precompiled` with a wrong digest | Ready, `precompiled: false` (falls back to source) |

The app itself was not started here, so that your `portable/` profile is not
changed.

## Your checks

Open Tools › Messages and set verbosity to 2 (debug) for checks 4 and 5.

1. **Extensions click.** Open Settings › Extensions. The list shows without
   a hang. A package whose worker crashed shows "Stopped unexpectedly".
2. **Cached start.** Quit, then start again. Startup is not slower than
   before; the bundled add-ons are not prepared again.
3. **Network-file Close.** Play an audio file from a network share, then
   Close. The app exits promptly.
4. **Cursor.** Use the app normally for a few minutes. Each helper start
   logs "Add-on helper launch N: <program>". Note whether the spinner cursor
   appears, and whether N rises while it does. Every helper now starts with
   `STARTF_FORCEOFFFEEDBACK`.
5. **Owner on.** Start with `vlc.exe --metadata-native-owner`, with
   Settings › Privacy › metadata network access on. Play a tagged music file.
   Artwork appears, and the log shows helper launches for
   `vlc-addon-worker.exe` while it is fetched.
6. **Subtitle53.** The plan names this check, but this repository does not
   define it. Run it as you did before.

Step 5 exits when you confirm these checks, or when a failing check has a
specific defect and a way to reproduce it.
