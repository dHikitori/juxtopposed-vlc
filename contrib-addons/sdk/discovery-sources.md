# Discovery source contract

This page describes the `discovery@1` contract for a media source.

## Contribution

A source needs a `discovery.source` contribution, a `discovery@1` contract, and a `wasm-message-v1` runtime. Declare each supported operation in `operations`.

```json
{
  "id": "stations",
  "type": "discovery.source",
  "contract": "discovery@1",
  "operations": ["roots", "browse", "search", "resolve"]
}
```

An absent operation list permits no operations. The host does not infer support from the contribution type. Network access and credential use need separate permissions.

## Requests

The host invokes an operation through the common worker message protocol. The request context has these fields:

| Operation | Context fields |
| --- | --- |
| `roots` | Optional `cursor` from a previous root page |
| `browse` | `nodeRef`: the source item ID of a selected folder; optional `cursor` |
| `search` | `query`, `filters`, and optional `cursor` |
| `suggest` | `limit`, optional `identity`, and optional `language` |
| `resolve` | `itemRef`: the stable source item ID; `intent`: `play` or `enqueue` |
| `subscribe` | `collectionRef`: the folder's source item ID, or empty for the root; `revision`: the last revision the source sent, or empty |

A query has a maximum of 512 UTF-16 code units. Filters have a maximum of 32 scalar fields. Each filter value has a maximum string representation of 256 UTF-16 code units. A suggestion limit must be from 1 through 50. Identity uses the fixed host media identity fields.

The source browser in Extensions uses `roots`, `browse`, `search`, and more pages. The queue uses `suggest` (see below).

## Change feed (`subscribe`)

While the user has a root or folder open (not a search), the host checks it
again. A root, folder or change-feed page can add:

- `revision`: 1 to 256 visible ASCII characters. The host sends it back in
  the next `subscribe`.
- `nextCheckSeconds`: when to check again. The host uses 60 to 3600 seconds,
  and 300 when the page has no value.

A `subscribe` reply is a page of new and changed items, plus `removed`: the
IDs of items to take away (at most 200). Report no change with `no_match`
and the same `revision`. New items appear first. An item that is also in
`removed` stays. A failed check ends the feed for that view; the items shown
stay. The host stops checking when the view closes or shows another page.

## Keeping the queue filled

When a source declares `suggest` and the queue can take source items, the
page offers **Keep the queue filled**. The host then asks `suggest` (with
`limit` only) whenever at most one of its suggestions is still ahead of the
playing item, at most every 5 seconds, and appends up to five. It never
removes or reorders entries, and it does not add an item it already added in
the same session. Each entry records the source name. The user can pause or
stop it; it also stops when the source or its account changes or after two
answers with nothing new. Return only items that can play from the queue.

## Pages

Return `match`, `partial`, or `no_match` through the common result protocol. A page has an `items` array, a `nodes` array, or both. Its maximum combined size is 200 entries.

```json
{
  "items": [
    {"id": "station-42", "kind": "media", "title": "Station 42", "mediaKind": "audio"},
    {"id": "regional", "kind": "node", "title": "Regional stations"}
  ],
  "cursor": {"offset": 200}
}
```

The `kind` defaults to `media`. An entry in `nodes` always has the `node` kind. A page cannot contain duplicate item IDs. The host removes entries with an incorrect ID or kind. An ID must be a nonempty string of at most 512 UTF-16 code units. Control characters and incorrect Unicode surrogate pairs are not permitted.

The host displays `title` as plain text, with a maximum of 512 UTF-16 code units. It limits `mediaKind` to 32 UTF-16 code units. Optional fields are `identity` and `available`. The host removes URLs, playback options, and other fields from the displayed entry.

A cursor can be JSON data with a maximum encoded size of 4 KiB. The host retains the cursor. The interface receives a separate opaque reference. This reference is valid only for the source, operation, view, generation, request context, and active source configuration that created it. Root pages use the same rules as browse and search pages.

## View lifetime

Each view shows at most 1,000 entries. A request has a time limit of 15 seconds.

Each view has its own item and cursor references. A reference from one view cannot act on another view. A new search cancels the previous request in that view. The host closes the view when it becomes hidden, its source changes, or its settings change. Late results cannot change a closed view.

## Saved items

When the user adds an item to the queue or a playlist, the host saves the
source, the item ID, the account and the displayed title. It never saves a
stream URL or a credential.

Use the same item ID for the same source item. A title change must not change
the ID. If a source has distinct versions of an item, the ID must identify the
version.

The account belongs to the host. A change to a credential or to a field in
`settings.accountFields` makes a new account, and items saved under the old
one no longer play. A code update, a title change or a new display language
keeps the account.

## Playback

Play and Add to queue need the `media.stream` permission. The user accepts it
when they install the add-on, or the app accepts it for a bundled add-on.
Turning the add-on off stops its items from playing. Browse and search do not
need it.

Add to queue saves the item without asking the source for a stream. When the
player opens the item, the host calls `resolve` with `intent: "play"` and
plays the stream it returns. Each open, including a replay, calls `resolve`
again. A package cannot supply player options, output settings or an access
module.
