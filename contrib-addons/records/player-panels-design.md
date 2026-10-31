# Player panels, and Episodes as an add-on (design)

Written 31 October 2026. Status: implemented on Linux, waiting for the
Windows check; see [Implementation](#implementation). It extends the
published hooks of
[UI, themes and configuration](../04-ui-themes-and-configuration.md) and the
read projections of [guest projections](guest-projections.md).

## Summary

An add-on can own a side panel in the player and put a button for it in the
player layout. The app keeps the panel frame, the button placement, file
access, the media library and playback. The add-on supplies the view, its
state and its commands.

The first package is a bundled **Episodes** add-on, on by default, that
replaces the built-in Episodes panel. The same hook also supports chapter
lists, lyrics, show notes, cast panels, transcript search and a chat panel
for a room session.

Five additions are necessary:

1. A `player.panels@1` hook with a `ui.panel` contribution.
2. Add-on commands as layout buttons, with package icons.
3. A context command that the app calls when the media changes, with an
   availability answer.
4. More view components: tabs, a menu, a media row and a paged list.
5. A `media.siblings` projection, watch state in `library.query`, and a
   `play` effect in a command reply.

## The built-in panel today

| Layer | Code | Work |
| --- | --- | --- |
| Find episodes | `util/series_discovery.hpp`, `player/series_model.*` | Parses file names, finds the show root, scans folders (local or network share) on a background worker, groups seasons and versions. Podcast rows come from `NetworkPlaylistModel`. |
| Add information | `player/episode_metadata.*`, `RedesignSeriesMetadata.qml` | Episode titles from `api.tvmaze.com` directly, or from the TVMaze add-on when the native metadata owner is on. Watch progress from the media library. |
| Show | `RedesignEpisodesView.qml`, `RedesignPlayerView.qml`, `RedesignPlayer.qml` | Season picker, rows, version menu, play and play from start. The Episodes button is in the layout; the panel uses the same side area as the queue and the playback settings. |

## What the add-on contract has today

- **Commands.** A `ui.command` in `player.actions@1` shows in the player
  menu (at most 16, `RedesignMenu.qml`). It is not a layout button, and the
  menu does not show the contribution `icon` resource.
- **Views.** `ui.route`, `ui.modal`, `ui.section` and `ui.replacement`
  render a `ui@1` document with the app's components: `Page`, `Section`,
  `Column`, `Row`, `Heading`, `Text`, `Notice`, `EmptyState`, `Button`,
  `IconButton`, `Image`, `MediaCard`, `List`, `Progress`. View icons are a
  fixed list of 19 names.
- **State.** A view gets its initial `state` from the manifest. After that,
  only a command reply changes it (`data.state` or `data.patch`, checked
  against `stateSchema`; at most 128 KiB, 4096 JSON values and depth 16).
  `ui.state.patch`, `ui.navigate` and `ui.modal.open` are names in the
  permission table only; no operation handles them.
- **Reads.** `queue.snapshot`, `library.query` and `media.fingerprint`,
  with opaque item IDs for each package and app run.
- **Playback.** Discovery items play through a gesture check
  (`authorizePlayable`). A session controller can send `playback.command`
  under a lease. A view command cannot start playback.

## Goals and limits

Goals:

- An add-on owns the content of a player panel and its button.
- The panel looks like the rest of the app, in every theme.
- A failed or turned-off add-on removes the button; the player stays usable.

Limits (from the rules in the handoff):

- No new network scope. The add-on gets no file paths, MRLs or feed URLs.
- Idle panels add no timers or polling. Basic UI does no package I/O.
- No file or network work on the GUI thread. Folder scans stay on the app's
  background worker.
- The add-on cannot replace playback-critical controls, permission prompts
  or recovery.

## Design

### 1. The `player.panels@1` hook

A new contribution type `ui.panel`, accepted only by `player.panels@1`:

```json
{
  "id": "episodes",
  "type": "ui.panel",
  "contract": "ui@1",
  "hook": "player.panels@1",
  "name": "Episodes",
  "label": "Show the episodes of this series",
  "icon": "icons/episodes.svg",
  "view": "views/episodes.json",
  "state": { "status": "loading" },
  "stateSchema": { "...": "..." },
  "command": "episodes.context"
}
```

The app owns:

- The frame in the side area that the queue and playback settings use. It
  sets the width, the title bar and the close button.
- Only one side panel at a time. Opening a panel closes the queue, the
  playback settings and other panels, as Episodes does now.
- Focus, Back, Escape, and focus restore when the panel closes.
- The loading frame, and the recovery state when the package fails.
- `controlsLocked` while the panel is open.

There can be many `ui.panel` contributions. Each one is a separate layout
item. When one panel and a built-in item have the same purpose, the user
chooses one of them in "Interface integrations", as for `ui.replacement`.

### 2. Layout buttons and icons

- Each `ui.panel` adds an item to the customization catalogue
  (`RedesignCustomizationCatalog.qml`) with the ID
  `addon:<package>/<contribution>`. The user can put it in any layout zone
  that accepts buttons. Its position is saved in the layout profile like a
  built-in item.
- A package can give a default zone (`footerRight`, `footerLeft`, `above`).
  The app puts the item there one time, on install or on first enable, and
  only if the zone has space. It never moves a built-in item. A user move
  wins after that.
- A missing or turned-off package removes the item from the layout but
  keeps its saved position, so the item comes back after a reinstall.
- The `icon` resource is a single-color SVG. The app checks it (size limit,
  no scripts, no external references, no raster images, one path color) and
  recolors it for the theme and the selected state. An icon that fails the
  check gets a generic icon; the install does not fail.
- The same icons become available to `ui.command` items in
  `player.actions@1`, so the menu can show them.

### 3. Context and availability

The panel button is visible only when the panel has something to show. The
add-on decides that, but only when the app asks:

- When the playing media changes, the app calls the panel's context command
  with `{revision, media: {kind, durationMs, id}}`. `id` is the opaque item
  ID of the guest projections.
- The reply is `data: {available: bool, state}`. The app shows the button
  only when `available` is true, and keeps the answer for that media
  revision.
- The app sends at most one context call for each media change. It cancels
  a call that is not complete when the media changes again. It does not call
  the add-on on a timer, while paused, or while the package is off.
- While the panel is open, a later context change (new watch state, a new
  title from a provider) sends one more context call, at most once a second.
- The view's own commands (choose a season, open details, refresh) get the
  same state reply as now.

This keeps the current rule: only a command reply changes view state. A
guest never pushes state, so `ui.state.patch` stays unhandled.

### 4. View components

New components, all from the app, all in `ui-view.schema.json`, the corpus
and the preview tool:

| Component | Use | Notes |
| --- | --- | --- |
| `Tabs` | Season picker, panel pages | Bound to a state list; a choice sends a command with the key. Becomes a combo box in a narrow panel. |
| `Menu` | Versions, play from start, more actions | A button that opens a list of actions. At most 16 entries. |
| `MediaRow` | An episode, a chapter, a lyric line | Thumbnail, title, secondary text, duration, progress, a "played" or "current" mark, a primary action and a menu. |
| `List` with `pageSize` and `more` | Long lists | The app draws only the visible rows. A `more` flag in state shows a "Show more" row that sends a command. |

Also add host icon names for panels: `episodes`, `chapters`, `lyrics`,
`people`, `more`, `check`.

State limits stay the same. A panel shows one page of rows at a time (one
season for Episodes); a season of 100 episodes with ten values each is
about a quarter of the 4096-value limit.

Each component needs the `04` checks before release: keyboard only, screen
reader labels, RTL, enlarged text, long translations, theme switching in an
open panel, and loading, empty and error states.

### 5. Data for the panel

All data is a copy. The add-on gets opaque IDs, never paths or URLs.

- **`media.siblings`** (new operation, permission `media.siblings`). The
  app finds the folder of the current media and scans it on its background
  worker, the same scan `SeriesDiscovery` does now. The response pages like
  `queue.snapshot`: `offset`, `limit` (at most 100), `total`, and `items`
  with `id`, `name` (the file name), `folder` (the path relative to the
  scan root, with no root), `durationMs` and `identity` (the app's parsed
  season, episode, absolute number and air date, which subtitles and
  artwork already use). For a podcast feed, `items` are the feed entries
  that the app read, with `title`, `published` and `description`. The app
  makes the scan only once for each media revision and keeps it while the
  panel is open.
- **Watch state.** `library.query` gets the fields `progress` (0 to 1),
  `played` and `lastPlayed` for `videos`, and a lookup by item ID.
- **Titles.** The app adds provider titles to `media.siblings` items as
  `title` with `titleSource`, under the same retention rules as now. Add-ons
  do not call other add-ons, so the Episodes package does not call TVMaze.
- **Artwork.** The app adds an asset handle to each item when it has one.
  The view shows it through the existing asset broker.

### 6. Playing an item

A view command reply can include one effect:

```json
{ "status": "ok", "data": { "effect": { "play": { "id": "video:…", "fromStart": false } } } }
```

- The app accepts the effect only in the reply to a command that a user
  click started. The click gives the dispatch a single-use gesture token
  that ends with the reply.
- The ID must be an item from the current `media.siblings` response or the
  queue projection. Anything else is refused.
- Play uses the app's own path (`Player.playSeriesEpisode` or the feed
  path), so resume, the queue and the media library behave as now.
- Permission: `playback.control` with the scope `"effects": ["play"]`.

### 7. Failure, speed and turning off

- **Off or removed.** The layout item and the panel go away. Nothing runs.
- **Crash or timeout.** The open panel shows the app's recovery state with
  "Reload" and "Turn off". The button stays. Quarantine after repeated
  crashes works as for other contributions.
- **First open.** The app shows the frame and the loading state at once.
  Bundled add-ons are ready about 1.7 s after start on Linux; the Windows
  time needs a measurement.
- **Budget.** Add to `performance-contract.md`: a context reply in less
  than 150 ms with a warm worker for a folder of 500 files, and no change in
  idle CPU with the panel closed.

## The Episodes add-on

The bundled package `org.videolan.episodes`, on by default:

- Permissions: `ui.contribute` (`player.panels@1`), `media.siblings`,
  `library.read` (`videos`: `title`, `durationMs`, `progress`, `played`),
  `playback.control` (`effects: ["play"]`). No network permission.
- It groups `media.siblings` items into seasons and versions, sorts them,
  and chooses the display title. The grouping rules move from
  `SeriesDiscovery` (`sameSeries`, `episodeKey`, `less`) into Rust.
- It answers the context call with `available` and the current season.
  Season changes, details and refresh are view commands.
- Its settings: hide played episodes, sort order, and the scan depth
  (within the app's limit).

The app keeps the folder scan, identity parsing, titles from providers,
watch state, artwork and playback. It removes `SeriesModel`'s grouping, the
built-in `RedesignEpisodesView.qml` and the direct TVMaze calls in
`EpisodeMetadata`.

## Work plan

| Step | Work | Proof |
| --- | --- | --- |
| 1 | Titles only through providers: remove the direct TVMaze calls | Existing title tests; needs the native metadata owner on by default |
| 2 | `player.panels@1`, `ui.panel` manifest rules, the panel frame, layout items, icon checks | Manifest corpus cases; QML tests for open, close, focus, Back, recovery |
| 3 | Context call and availability | Registry tests: one call for each media change, cancel on change, no call while off |
| 4 | `Tabs`, `Menu`, `MediaRow`, paged `List`, new icon names | View schema corpus; QML tests for keyboard, RTL, large text, theme switching; preview images |
| 5 | `media.siblings`, watch fields in `library.query`, the `play` effect | Projection tests (paging, no paths, IDs for each package); gesture tests (no click, old token, unknown ID) |
| 6 | The Episodes package and a `panel` template | The same file-name cases as `test_series_discovery.cpp` and `test_series_model.cpp`, run against the built-in code and the add-on; same seasons, order and versions |
| 7 | Bundle it on by default, remove the built-in panel | Linux labels; a Windows check with a show on an SMB share; performance budget |

Steps 2 to 4 are useful without Episodes. Step 6 must match the built-in
panel before step 7 removes it.

## Decisions

The user asked for implementation to start after this record, with the
choices left to the implementer. These are the choices:

- **No built-in fallback.** After the parity check, the built-in panel is
  removed. Turning the add-on off removes Episodes. The package is bundled
  and on by default, and "Turn off" in the recovery state is the only way a
  failure removes it.
- **Default zone.** A package can ask for a zone with `"placement"`. The
  app uses it one time, only if the zone has space, and never moves a
  built-in item.
- **Podcasts.** The first release covers feeds and files, so the add-on
  matches the built-in panel before the built-in panel goes.
- **Step 1 waits.** The direct TVMaze calls stay until the native metadata
  owner is on by default (a rule of the handoff). The panel does not
  depend on it: the app adds titles to `media.siblings` from whichever path
  is active.

## Implementation

Built on 31 October 2026 in `82fbb47ee3` (host), `1db521b813` (package and
bundling) and `54cc3444cc` (instruction budget). The author guide is
[player panels](../sdk/player-panels.md).

### What the app has now

- **Hook and frame.** `ui.panel` on `player.panels@1`
  (`addon_manifest.cpp`, `addon_ui_registry.*`), `RedesignAddonPanel.qml`
  in the side area of the queue and playback settings. One side panel at a
  time; Escape and the close button close it; a failed view shows "Reload"
  and "Open Extensions", and the button stays.
- **Context call.** `AddonUiRegistry::setPanelMedia` on each new media,
  `refreshPanels` (one call a second at most) when the series data changes
  while a panel is open, `attachPanelSession` when a panel opens. Replies
  for older media are ignored.
- **Effects.** `play`, `rescan`, `identify` and `source`, accepted only in
  the reply to a session command (a click). `AddonMediaSiblings::runEffect`
  checks the ID; the player runs the effect on its existing paths.
- **Layout and icons.** Layout items `addon:<package>/<panel>`, saved
  layouts with `episodes` moved to the bundled panel, requested zones drawn
  until the user removes them (`dismissed` in the saved layout), a
  "missing" item visible only in the editor. `addon_panel_icon.cpp` checks
  the SVG in the store snapshot job; `RedesignIcon.markup` draws it.
- **Components.** `Tabs`, `Menu`, `MediaRow`, and `List` with `scroll`,
  `more`, `moreAction` and `moreText`, in `AddonUi.js`, the renderer, the
  host type list, `ui-view.schema.json` and the corpus.
- **media.siblings.** `addon_media_siblings.*`, mounted on an app-owned
  `SeriesModel` (`redesignSeriesModel`). The series is looked for only
  while an enabled panel can read it (`siblingsWanted`).
- **Episodes package.** `examples/episodes`, version 1.0.0, bundled and on
  by default. The built-in `RedesignEpisodesView.qml` is removed.

### Changes from the design

- **Watch state** is in the `media.siblings` items (`progress`, `played`),
  not new `library.query` fields. The series model already reads it, and a
  panel then needs no `library.read` grant.
- **Details on request.** Descriptions, artwork and the title source come
  only for up to four IDs in `details`. A podcast's show notes can be 4 KiB
  for each entry.
- **Pages of 40.** The worker gives each dispatch 10 million Wasm
  instructions. A measurement with a probe worker gave about 120,000 for
  each rendered row (25 rows: 3.2 M, 50: 6.1 M, 100: 12.0 M, so 100 rows
  stopped the worker). After the change a closed row costs about 92,000
  and a page of 40 about 37 % of the budget. Longer seasons page with Next
  and Previous, and the playing episode's page opens first
  (`currentOffset`). The open-panel flow takes 1.1 ms (median, 500 files,
  three host reads) in the real worker.
- **Grouping.** The app's scan still matches the series and orders the
  files; items are in episode order with the versions of an episode after
  version 0. The package makes episodes, seasons, labels, marks, versions
  and pages from that. The scan stays in the app because a package must not
  see the other files of a folder.
- **Default zone.** Requested zones are drawn, not written, so every
  layout write stays in the editor's save. The bundled panel has no
  `placement`: the default layout names it, and a layout saved without the
  built-in button keeps it removed.
- **No `panel` template.** The Episodes package and the guide are the
  example. The icon names `lyrics` and `people` are not added: the app has
  no glyphs for them yet.

### Evidence

| Check | Result |
| --- | --- |
| `test_qt_addon_ui_registry` | 26 cases, including context calls, stale replies, refresh limit, click-only effects, panel declarations and icon checks |
| `test_qt_addon_media_siblings` | 6 cases: no paths or URLs, IDs for each package, grants, season filter and pages, waiting for a scan, details, effects |
| `test_qt_addon_schema_corpus` | 330 cases, including the Episodes manifest, view, icon and `tests/sample-state.json` against the app's rules |
| `check-corpus.py` (jsonschema 4.23.0) | 326 cases |
| Episodes `cargo test` | 17 tests: the former built-in rules case by case, paging, details, effects, fixture |
| `addon_episodes_worker` | 6 tests in the real worker, including a full page with long names and an open row with 4 KiB of notes |
| QML | `tst_AddonUi` (components), `tst_AddonMounts` (frame), `tst_RedesignPlayerLayout` (panel items, migration, dismissal), `tst_RedesignPlayerChrome`, `tst_RedesignCustomizationEditor`, `tst_AddonPlainText` |
| Labels at `1db521b813` | profile 33 (+1 expected fail, 1 skipped), ui 11, source 23, metadata 53, session 5, visualizer 12, app 6; 0 failed |

Two QML results fail on the commit before this work as well:
`tst_AddonTheme::test_unknownTokensAndExecutablePropertiesFailClosed`, and
`tst_Redesign`, which passes in 31.3 s but has a 30 s meson limit.

### Open

- The user's Windows check of the bundled Episodes panel.
- Step 1 (titles only through providers) waits for the native metadata
  owner, as decided above.
- Text in the package's state and view is English only. A state message
  with arguments, or the app's locale in the invocation, would let the
  package translate it.
- `SeriesModel` still has the season list and roles of the removed view;
  only `siblings()` and the scan are used now.

