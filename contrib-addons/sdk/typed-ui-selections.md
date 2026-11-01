# Typed UI selections

This page describes the host selection adapter. The subtitle and playlist dialogs can use an explicitly selected replacement. Core keeps the built-in body available.

## Host choices

The host owns each selection session. It creates a bounded choice set from current core data. The host keeps the mapping from choice references to native playlist IDs, media URLs, and prepared subtitle handles. The package receives only this public projection:

```json
{
  "choiceSet": "host-session:3",
  "targets": [
    {"ref": "target:0", "title": "Films", "enabled": true}
  ],
  "assets": []
}
```

The projection can have at most 200 targets and 200 assets. Each title has a maximum of 512 UTF-16 code units. A reference is valid only within its choice set. A title does not identify a target. Two playlists with the same title have separate references.

Subtitle media titles need a separate `metadata.context` grant for `title`. Without that grant, the package receives numbered media labels. Core can show the actual media names in its private selection summary.

The host creates a new generation when the offered choices change. The caller must also change the choice set when media, account, or target revisions change. The component rejects the previous generation immediately, before it creates the next view session.

The package declares the parameter schema for this projection. It also declares `resultSchema` and `stateSchema`. Its commands can update `state.result` through the normal bounded UI session protocol.

## Playlist result

An existing playlist uses one offered reference:

```json
{"choiceSet":"host-session:3","selected":["target:0"]}
```

A new playlist uses an empty selection and a name:

```json
{"choiceSet":"host-session:3","selected":[],"newName":"New playlist"}
```

A name has a maximum of 256 UTF-16 code units. It cannot contain control characters or incorrect Unicode surrogate pairs. The host removes spaces from each end. A result cannot select an existing playlist and name a new playlist at the same time. A read-only choice cannot be selected.

The playlist replacement needs the exact UI hook grant. It also needs `library.read` with the `playlists` collection and the `title` field before the host supplies playlist titles.

## Subtitle result

A subtitle result assigns offered assets to offered media targets:

```json
{
  "choiceSet": "host-session:4",
  "assignments": [
    {"targetRef": "target:0", "assetRefs": ["asset:0", "asset:1"]}
  ]
}
```

A result has at most 200 assignments and 1,000 total asset references. Each assignment has at most 200 asset references. A target cannot appear twice. An assignment cannot contain duplicate assets. Unknown, stale, disabled, or empty references fail validation.

An empty `assetRefs` array explicitly clears the selection for its known target. An empty `assignments` array does not specify a selection and is rejected. The containing core dialog defines which track selections the offered choices can change.

The subtitle adapter changes the offered online selections. It keeps unrelated local track assignments. It does not save unselected automatic guesses.

The package cannot supply a subtitle URL or a local file path. The host resolves each selected reference through its private map. The existing subtitle preparation path must validate and download remote handles before core applies a track assignment.

## Core controls and authority

Core supplies Confirm, Cancel, and Use built-in view outside the package document. It also shows the validated destination or selection counts outside that document. Confirm first validates the draft against its declared result schema. It then validates the fixed host result contract and current choice set. A choice generation can confirm only once, including after a registry session reload. Cancel returns no selection and closes the session.

The host component can use the containing dialog's core actions instead of its own action row. The package view cannot remove those actions. Recovery returns to the built-in body. A missing data scope, failed view, or absent host validator also returns to the built-in body.

A correct result supplies a selection. It does not supply queue, playlist, or track mutation authority. The core dialog must retain its current-item, destination-revision, preparation, and final commit checks.

The playlist picker saves through the app's own playlist path. It does not add discovery source items to the library.
