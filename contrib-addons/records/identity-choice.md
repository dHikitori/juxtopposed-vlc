# Identity choice dialog and accepted identity (Step 6.1)

Step 6, row 1 of the [continuation plan](../07-continuation-plan.md).
Written 30 October 2026.

## What the user sees

Episodes › Episode options › **Choose series…** opens a dialog. It searches
the add-on identify providers with the episode's series name (the user can
change the search text) and lists the series found, with year, season and
episode, and source. Nothing is saved until the user selects a row and
chooses **Use series**, also when there is only one row. Closing the dialog,
or playing another item, cancels the search.

## How it works

- `AddonIdentityChoice` (`addons/addon_identity_choice.*`) is the trusted
  owner. It starts a manual native anchor on the metadata service's backend
  for the current item, binds it as an identity target, and calls
  `identify("video.identity")`. QML gets display rows only; the choice
  references stay in C++.
- On a choice it calls `acceptIdentity`, then `enrichSelected` for the
  episode title. The title goes through the ordinary native commit:
  `CopyLocalItem`, `PrepareLocal`, `Complete`, then `Apply` on the exact
  original item, with Title in `replace_fields` because the user chose it.
- `AddonAcceptedIdentities` (`addons/addon_accepted_identities.*`) saves the
  accepted series in `<profile>/accepted-identities.json` with the next
  profile revision. Writes take a lock file and replace the file
  atomically; every read loads the file again, so other windows see a new
  choice without a timer. Forgetting a choice also takes a revision. At most
  4096 records.
- Later automatic episode lookups (`EpisodeMetadata`) add the accepted IDs,
  title and year to their request, and the revision to its context, so
  results cached under an earlier guess are not reused. A new choice clears
  the titles found under the old guess and looks up again.

## Provider

TVMaze 0.2.0 adds `identify` for `video.identity`. It searches shows and
offers up to eight, each as an episode identity with the TVMaze show ID and
the file's own season and episode, so `enrich` then looks up by ID.

## Tests

| Test | Label | Result |
| --- | --- | --- |
| `test_qt_addon_identity_choice` | metadata | 8 cases pass: explicit choice to native Apply and revision, one offer still waits, manual query, cancel, failures, revisions across two store instances |
| TVMaze `cargo test` | — | 19 pass, 4 new for identify |
| `addon_tvmaze_worker` (now registered) | metadata | 4 pass, including identify then lookup by the chosen ID |
| `addon_app_smoke --driver` | app | Pass; the player loads the dialog with no QML warning |

Not run: a live TVMaze search in the app, the dialog in a QML test, and
Windows. The bundled TVMaze package in an existing profile stays at 0.1.1
until distribution upgrades exist.
