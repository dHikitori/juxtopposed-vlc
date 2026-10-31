# Player panels

A player panel is a side panel in the player with a button in the player
layout. The app owns the frame (title, close button, focus, Back and Escape,
scrolling, recovery) and the button placement. Your package supplies the
view inside the frame, its state and its commands.

The bundled Episodes package ([`examples/episodes`](../examples/episodes))
is a complete panel. The [design record](../records/player-panels-design.md)
gives the reasons for each rule.

## Declare the panel

```json
{
  "id": "lyrics",
  "type": "ui.panel",
  "contract": "ui@1",
  "hook": "player.panels@1",
  "name": "Lyrics",
  "label": "Show the lyrics of this song",
  "icon": "icons/lyrics.svg",
  "view": "views/lyrics.json",
  "command": "context",
  "placement": "footerRight",
  "stateSchema": { "type": "object", "additionalProperties": false, "properties": {} },
  "state": {}
}
```

- `name` is the panel title and the layout item name. `label` is the button
  tooltip and screen reader text.
- `command` names a `ui.command` of the same package that has a `command`
  operation and no `route`: the context command.
- `placement` (optional) is the zone where the button first appears:
  `headerLeft`, `headerRight`, `footerLeft`, `footerRight` or `above`. The
  app adds it when it draws the layout. If the user removes it in the
  editor, it does not come back. A panel without `placement` is only in the
  editor's catalogue.
- The permission `ui.contribute` must list `player.panels@1`.

## Icon

A single-color SVG with `viewBox="0 0 24 24"` and only `g`, `path`,
`circle`, `ellipse`, `rect`, `line`, `polyline` and `polygon`. The app
replaces each `fill` and `stroke` color with the theme color, so any color
works; `none` stays `none`. The app refuses scripts, styles, `href`, text,
images, entities and other namespaces, and files larger than 16 KiB. Then
the button gets the generic icon, and the package still installs. Check an
icon with the preview tool or `test_qt_addon_ui_registry`.

## The context command

The app calls the context command when the playing media changes, one call
for each panel. The invocation context has:

- `media`: `revision` (it changes with each media), `kind` (`video`,
  `audio` or `none`) and `durationMs` when known. Never a URL, a path or a
  title.
- `panel`: the panel's contribution ID.
- `state`, `parameters`, `ui`: only when the panel is open (see below).

Reply with `data.available`: `true` shows the button, anything else hides
it. A reply for an older media revision is ignored.

When the panel opens, the app calls the context command again in the
panel's session, so the reply can also set `data.state` (or `data.patch`)
like any view command. While the panel is open, the app calls it again when
its own data changes (titles, watch state, artwork), at most once a second.
It never calls it on a timer, while the package is turned off, or for a
closed panel after the first call.

## View commands and effects

The view's own commands work as for any `ui@1` view: a click sends the
command with its arguments and the current `state`, and the reply can
replace or patch the state.

A reply to a command that a click started can also ask the app for one
effect:

```json
{ "status": "match", "data": { "effect": { "play": { "id": "media:…", "fromStart": false } } } }
```

| Effect | Value | The app |
| --- | --- | --- |
| `play` | `id`, optional `fromStart` | Plays that item on its own path (resume, queue and library as usual). |
| `rescan` | optional `chooseFolder` | Looks for the episodes again; with `chooseFolder`, asks the user for the folder first. |
| `identify` | `{}` | Opens the series choice. |
| `source` | `id` | Opens the item's title source page (HTTPS only). |

The app ignores an effect in a reply to its own context call, an unknown
effect, and an ID that the package did not get for the current media.

## media.siblings

Permission `media.siblings` with `sources`: `folder`, `feed` or both. The
app finds the files of the same series in the media's folder (on its own
background worker), or the entries of the media's podcast feed.

Request (all optional): `season` (-1 to 9999), `offset` (0 to 10000),
`limit` (1 to 100, default 50), `details` (up to 4 item IDs).

Reply:

| Field | Meaning |
| --- | --- |
| `kind` | `folder`, `feed`, or `none` when the media has no series or the source is not granted. |
| `show`, `message`, `loading`, `limited` | Show title; an app message for the user; a scan or feed is still loading; a scan stopped at its limits. |
| `seasons` | `[{season, count}]` in order; `count` counts episodes, not versions. |
| `currentSeason`, `currentOffset` | The playing item's season, and its index in the selected list. |
| `total`, `offset`, `items` | The selected items and this page of them. |
| `details` | `[{id, description, artwork, hasSource}]` for the requested IDs. |

Each item has `id` (`media:` and 32 hex digits, different for each package
and app run), `kind`, `title`, `season`, `episodes`, `absolute`, `airDate`,
`published`, `version`, `progress`, `played`, `current` and `video`. Folder
items also have `name` (the file name) and `folder` (relative to the series
folder). Items are in episode order, and the versions of one episode follow
each other, version 0 first.

The read waits up to 8 seconds for a running scan. Ask for `details` only
for the rows you show in full: a podcast's show notes can be long, and
artwork for a file can need a thumbnail.

To show artwork, put the detail's `artwork` value (a `media:` handle) in an
`Image`, `MediaCard` or `MediaRow` `artwork` binding. Only the app can
resolve these handles.

## Components for panels

| Component | Use |
| --- | --- |
| `Tabs` | One choice from a state list of `{key, label}`: buttons for up to four entries, a combo box for more. In the action, `item.tab` is the chosen entry and `item` is the enclosing list item. |
| `Menu` | A button with up to 16 commands; entries can have `visible`. |
| `MediaRow` | Number, title, `subtitle`, `mark` (accent color), `progress` and `artwork`; with `expanded: true` it shows the artwork and its `children`. |
| `List` | `"scroll": false` shows every row and lets the frame scroll; `more`, `moreAction` and `moreText` add a "next" button. |

New icon names: `episodes`, `chapters`, `more`, `check`, `replay`,
`play-outline`.

## Limits that shape a panel

- **Instructions.** Each dispatch can use 10 million Wasm instructions. In
  Rust with `serde_json`, building one row of state costs about 100,000,
  mostly allocation. Episodes shows 40 items on a page and leaves empty
  values out of closed rows. A missing text or flag binding shows nothing.
- **State.** At most 128 KiB, 4096 JSON values and 16 levels, checked
  against your `stateSchema` on each update.
- **Text.** Dynamic text in state is shown as it is. Use `message` keys
  from your locale files for fixed text in the view.

## Test a panel

- `vlc-addon-validate --ui …/AddonUi.js <package>` checks the manifest,
  view and icon.
- `vlc-addon-preview` writes the view with its initial state in both
  themes. Put a realistic state in a copy of the manifest to look at a full
  panel.
- Run your module in the real worker with host replies, as
  `examples/episodes/tests/test_worker.py` does, and include a test for your
  largest page.
