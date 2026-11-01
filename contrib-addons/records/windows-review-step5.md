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

## Fourth user run: Add Subtitles and Radio Browser

- **Add Subtitles showed only "The add-on store is busy".** The provider
  list is read on background work and refused at once when another reader
  held `store.lock`. Fixed in `c8f5b39d86`: the read reports a held lock
  and provider selection retries for up to 10 s.
- **Radio Browser still froze the window ("Not Responding"), with the
  package on a local drive.** The stall report (`--qt-hang-report`),
  resolved against the build's symbol table, showed:
  - a 17.1 s stall with all 40 samples in Qt's
    `QWindowsDirectWriteFontDatabase::populateFamily`, under
    `QFontEngineMulti::ensureFallbackFamiliesQueried` while a list row was
    laid out. The first text needing a fallback font made Qt read every
    installed family (543 font files here) on the GUI thread.
  - a 1.7 s stall in `AddonManager::packages()`, which read and hashed
    every package on the GUI thread.

  A native probe built against the same static Qt 6.8.3 measured the first
  fallback layout at 288–391 ms cold and 1 ms after reading each family's
  writing systems first. Fixed in `ac63876518` (that read runs at startup
  on a low-priority thread) and `fbbf9a3c5d` (package reads cached by store
  token and state-file stamp; a new worker verifies its package again).

Package `ac63876518`: `vlc.exe`
`2434fe13bd273922f9e52fef60f2959ccb2190f8d1b42fbb42b4df98c36e390b`,
`libqt_plugin.dll`
`ae5d86db7094c927fe52c0a2d560b96c07976b8e554c05475ce8c1294fc400e4`.

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

## Final package (Steps 6 and 7)

Package `42503bc586` (clean tree): `vlc.exe`
`b1a42a8fbc2db33cba9a1c85ff7f7d8c7d50c8ba5704c7436b1bfc52dd1eebb5`,
`libqt_plugin.dll`
`a3b5a1557b43f69cb9b2c721ec1ef221ca8dc072ab57afa1af04bb61ff66c3da`.
It adds every Step 6 row and the Step 7 host fixes. The checks from the
fourth run (Add Subtitles, Radio Browser, settings, theme, artwork, radio
loading) apply again. New checks:

1. **Identity choice.** Play an episode, then Episodes › Episode options ›
   "Choose series…". Pick a series; the episode titles follow it.
2. **TVMaze 0.3.0.** Extensions shows the bundled update. Episode titles
   still appear, and come back after a restart without a new lookup.
3. **Settings.** Open TheSubtitleDB settings; Save and Cancel work. A field
   with a folder control (none bundled) is covered by tests only.
4. **Temporary settings.** Change a TheSubtitleDB setting and select "Use
   until the app closes". The note shows; "Stop temporary settings" removes
   it; a restart forgets it.
5. **Discovery.** Open Radio Browser. A root page loads without a freeze.
   If Play and Enqueue are available for the source, "Keep the queue
   filled" is not shown for Radio Browser (it has no suggestions); the
   fixture source in tests covers it.
6. **Owner on** (optional): start with `--metadata-native-owner` as before.

## Package with player panels

Package `5938f2171b`: `vlc.exe`
`dd20dfa007f6c06fc1ee669185ec4b549812be9290b84dca2716aed8fedd72a9`,
`libqt_plugin.dll`
`fada14da8117736e5832aa0661e25f084145c60ac7effe40f67f7a83c8d15291`.
It is `e7d48e7f54` plus log lines for why an add-on view stops
(`46c393f493`, for the Episodes run below), the library toolbar
staying at the top while a list loads (`183c841492`, check 13), and the
loading message in the content area without an empty message before the
rows (`002c0466b5`, check 14), and Radio Browser playback access and a
faster Add to playlist (`aae2a5d9f0`, `5938f2171b`, check 15).
`e7d48e7f54` adds a memory cache for library thumbnails (`a00be55dad`, check 11)
and a fix for the crash on opening Network › Radio (`e7d48e7f54`, check
12) to `5d71509ec5`, which added the startup update prompt and evener
Extensions rows (checks 9 and 10); Episodes stays 1.0.2.
`BUILD-INFO.json` says `dirty` only because untracked worktree folders are
under `.claude/`; the source copy takes files only, so the build is the
committed tree. The Episodes panel is the bundled add-on
`org.videolan.episodes` 1.0.2 ([record](player-panels-design.md)).

Earlier packages of this section: `05b791e4a2` showed no button (1.0.0
listed no command operations); `14953b093d` (1.0.1) showed the button, but
the panel filled late, no other episode opened, and it looked plainer than
the built-in view. 1.0.2 and this app fix all three; the record has the
side-by-side comparison.

The checks above apply again; check 1 now goes through the add-on's menu.
New checks:

0. **Update.** The Extensions row for Episodes says "Update to 1.0.2
   available". Select it, review the app version (no new permissions) and
   update. A new profile gets 1.0.2 directly.
1. **Button.** Play an episode file from a series folder (a local folder
   and an SMB share). The Episodes button appears between Playback
   settings and Queue, with the same icon as before. It does not appear
   for a movie or a song.
2. **Panel.** Open it right after the episode starts. It fills without
   waiting and looks like the built-in panel did: one header with the show
   and season, "…" and close, and no second "Episodes" title. The playing
   episode is highlighted and open, marked "Now playing", with its artwork
   across the width, its versions, file name and "Playing"; watched
   episodes say "Watched". Other rows open and close, and each shows its
   thumbnail once made (a file on a share can take a moment), and "Play" or "Play
   from beginning" work. With several seasons the header opens the season
   list (a ✓ on the playing season), and a season returns to its episodes.
   Escape and the close button close the panel; Queue and Playback
   settings close it too.
3. **Menu.** "Episode options" › Refresh, "Choose series folder…" and
   "Choose series…" work as before.
4. **Long season.** A season with more than 40 files shows "Next
   episodes"; the playing episode's page opens first.
5. **Podcast.** Play an episode from a podcast feed. The panel lists the
   feed entries; "Listened" marks finished ones; "Play video" plays the
   video version when the feed has one.
6. **Turn off.** Extensions › Episodes › turn off. The button goes away
   and nothing scans the folder. Turn it on again and the button returns
   for the next episode.
7. **Layout.** Player customization shows "Episodes" in the palette;
   moving and removing it and saving works, and a removed button stays
   removed after a restart.
8. **Speed.** Starting an episode from a share and opening the panel
   feel the same as before.
9. **Extensions rows.** Each row's name, status and switch sit in the
   middle of the row, with even space above and below. A bundled update
   shows an "Update to …" badge beside the switch.
10. **Update prompt.** In Extensions select Episodes › More › "Use previous
    version" (back to 1.0.1), then restart the app. After start a dialog,
    "Add-on updates", lists Episodes 1.0.1 → 1.0.2. "Update all" installs
    it there; Episodes stays on and its panel works. Roll back once more
    and choose "Later": the dialog does not return on the next start (it
    asks again only for a newer version), and the Extensions row still
    offers the update. An update that asks for new permissions (none in
    this package) lists them and has its own "Approve and update".
11. **Thumbnails.** Open a large video folder in grid view and wait for
    its artwork. Scroll far down and back up: tiles you have already seen
    show their pictures at once instead of filling in again. Changing a
    folder's `folder.jpg` still shows the new picture.
12. **Radio.** Network › Radio lists countries without freezing or closing
    the app; a country lists its stations.
13. **Loading header.** Open Network › Podcasts (or a slow share): while it
    says "Loading…", the toolbar and breadcrumbs stay at the top.
14. **Loading message.** Open Network › Radio and then a share folder.
    While each loads, "Loading…" shows in the middle of the content area
    (not in the toolbar), and "This source has no media." does not show
    before the list appears.
15. **Radio Browser playback.** Media sources › Radio Browser: search for a
    station and select it. "Playback access" is enabled; approve the
    source there. Play then starts the station, Add to queue adds it, and
    Add to playlist returns at once without "Not Responding". The station
    plays from that playlist.

### Radio Browser cannot play (package `3795ac8c5f`)

Play and Add to queue stayed disabled, "Playback access" was disabled too,
Add to playlist made the window stop responding, and the added station did
not play.

- Play and Add to queue need the user's source approval, and the
  "Playback access" button is enabled only when the source lists
  `resolve`. The app plays through the native source owner, so the
  discovery service has no playable gate, and the coordinator dropped
  `resolve` from every listed source. The source could never be approved,
  so the playlist entry could not play either. Fixed in `aae2a5d9f0`.
- Add to playlist checks the source three times, and each check read and
  hashed every installed package (about 1.8 MB) on the GUI thread. From
  Windows over `\\wsl.localhost`, one such scan took 0.33–0.5 s.
  `5938f2171b` uses the cached read that the package list already uses.
  The append still reads the package and writes `source-admissions.json`
  on the GUI thread once; that is smaller, but it is still file work on
  the GUI thread.

### Network › Radio crash (package `5d71509ec5`)

The window froze for about 20 s and then closed. The Windows crash report
(`vlc.exe.45712.dmp`) showed an access violation in `libqt_plugin.dll` at
`QObjectPrivate::connectImpl`, under `NetworkDeviceModelPrivate`'s
constructor as the Radio list's model was created; the freeze was Windows
writing the dump. `e4f8923a76` connected the loading timer with the model
as context from the private's constructor, which runs before the model's
QObject base is built. Fixed in `e7d48e7f54`: the timer is its own
context.

### Episodes "stopped working" on first open (package `e7d48e7f54`)

On `Silicon Valley - S01E01`, opened about 9 s into playback from
`\\sojevic-nas\media\Series\Silicon Valley\Season 01`, the panel showed
"This panel stopped working" at once. No crash dump, and nothing was
logged: the failure paths were silent. Not reproduced here:

- The app smoke test now opens Episodes on a copy of that layout (titled
  episodes with `-thumb.jpg` files, more seasons, a
  `Show.S06.1080p.WebRip.H264` folder) and the panel lists the season.
- Worker states built for eight episodes with watched flags, progress,
  artwork handles and long descriptions over six seasons pass the view's
  own checks (`AddonUi.js`) and the manifest's state schema.
- The installed Episodes 1.0.2 is byte-for-byte the bundled one.

What is left is something only the share or Windows shows, for example a
scan that is still running when the panel opens. `46c393f493` logs the
reason. Run with:

    vlc.exe --file-logging --logfile=%TEMP%\vlc-log.txt --log-verbose=2 --qt-hang-report

and look for lines starting `Add-on view` or `Add-on ... stopped`.

Later the same day Episodes opened normally on The Walking Dead, so the
failure is intermittent. The diagnostic run has not happened yet; no
`vlc-log.txt` was written.
