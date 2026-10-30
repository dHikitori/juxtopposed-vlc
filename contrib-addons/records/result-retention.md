# Metadata result retention and expiry (Step 6.3)

Step 6, row 3 of the [continuation plan](../07-continuation-plan.md).
Written 31 October 2026. Implements the retention and cache rules of
[providers and discovery](../03-providers-and-discovery.md) (lines 65, 117,
123 and 125).

## Before

The coordinator kept `match` and `partial` answers for 60 s and `no_match`
for 15 s, whatever the provider permitted. Accepted episode titles from any
provider were saved in the media library without a provider statement.

## The record

A metadata result can carry `data.retention`
([SDK guide](../sdk/metadata-completion.md#retention)):

- `reuseSeconds` (0 to 86400): how long the host may reuse the answer.
- `keep` (`"memory"` or `"library"`): whether accepted fields may be saved.
- `offlineSeconds` (0 to 604800): how long an expired answer may be shown,
  labeled, while the provider is offline.

`ResultRetention` (`addons/addon_result_retention.hpp`) reads the record and
applies host limits for each status:

| Status | Default reuse | Maximum reuse | Saving and offline use |
| --- | --- | --- | --- |
| `match`, `partial` | 10 min | 24 h | As the record permits |
| `no_match` | 1 min | 1 h | No |
| `ambiguous` | 1 min | 10 min | No |
| Other statuses | none | none | No |

A record with an unknown key, a value of the wrong type, a fraction or a
value out of range permits nothing. Without a record, fields stay in memory
and no expired answer is shown: a provider that cannot state its retention
does not cause indefinite saving.

## Host behavior

- **Reuse.** `AddonMetadataCoordinator::remember` stores an answer only when
  reuse or offline use is permitted, and removes an earlier entry otherwise.
  Keys keep the release, account and settings partition and the policy
  generation, so another account never sees the entry. Ambiguous
  identification results are now reused briefly; they were not cached
  before. The cache stays in memory, with 128 entries; a full cache first
  drops fully expired entries.
- **Offline use.** When a provider answers `offline`, or is waiting after an
  offline answer, an expired entry inside its offline window replaces the
  answer. The consumer result has `data.stale: true`, and each field's
  provenance has `stale: true`.
- **Saving.** Field provenance carries `retention.keep`. The episode title
  adapter (`HostMetadataCache::mayPersist` in
  `util/episode_title_cache.hpp`) saves a title from an add-on provider
  (`package/contribution`) only when `keep` is `library`. Local tags and
  host sources have no package part and are saved as before.
- **Receipts.** A strict `no_match` receipt accepts a correct retention record
  and refuses a record that is not correct.

TVMaze 0.3.0 declares `reuseSeconds` 3600, `keep` `library` (TVmaze data is
CC BY-SA) and `offlineSeconds` 86400 on its matches and identification
candidates.

## Tests

| Test | Label | Result |
| --- | --- | --- |
| `test_qt_addon_metadata` | metadata | 92 pass, 8 new: provider expiry after 1 s with library keeping; host default without a record; no reuse and 7 incorrect records; limits for each status; partial results; ambiguous reuse; labeled offline use, also during the offline wait, and not for another account; no offline use without permission |
| `test_qt_addon_metadata_attempt` | metadata | New rows: a no-match with a correct record mints a receipt; with an incorrect record it does not |
| `test_qt_addon_episode_metadata` | metadata | New `anAddOnTitleIsSavedOnlyWhenItsRetentionPermits` |
| TVMaze `cargo test` | — | 20 pass, the match result has the record |

All labels pass: metadata 53, profile 28, source 22, ui 7, app 6.

Not done: Core Apply and the media library do not check `keep` for portable
fields written through the native owner (activation stays off). The player
does not yet show the offline label to the user. Not run on Windows.
