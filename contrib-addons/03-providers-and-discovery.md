# Metadata providers and service discovery

## One shared identity, several independent features

Use a host-owned `MediaIdentity` transport schema based on the redesign's existing provider-independent concepts in [media_identity.hpp](../modules/gui/qt/util/media_identity.hpp) and the rules in [subtitle-identification.md](../doc/subtitle-identification.md). Do not publish the current C++ structure as a binary ABI.

An identity includes media kind; original/alternative titles; scoped external IDs; relevant years/dates; artist/album information; podcast feed/GUID; episode numbering scheme and numbers; edition/release evidence; and provenance/conflicts. Unknown values remain unknown. A checksum identifies a particular file matching method, not a universal work ID. Host-private media handles identify local targets without giving a provider a filesystem path.

Movie, series, season and episode IDs are distinct scopes even when they use the same external namespace. Podcast GUIDs are scoped to a feed. Music track, recording, release and release-group IDs are distinct. Date-based, absolute and season/episode numbering cannot be converted without explicit mapping evidence. A combined episode file can cover multiple episodes. Artwork states its owner and role, such as series poster, episode thumbnail or album cover.

Publish initial feature keys, extendable through versioned contracts:

| Feature | Typical outputs |
| --- | --- |
| `video.identity` | Movie/series/episode identity candidates and identifier mappings |
| `video.details` | Descriptions, episode names/numbers, dates, cast and related work references |
| `video.artwork` | Posters, backdrops, season covers and episode thumbnails with owner/role |
| `podcast.identity` | Feed/series/episode identity and mappings |
| `podcast.details` | Names, episode numbers, publication dates, descriptions and artworks |
| `music.identity` | Track/recording/album/artist identity and mappings |
| `music.details` | Titles, credits, track/disc numbers, release information and artwork references |
| `subtitles` | Subtitle candidates, language/SDH/forced flags, compatibility and download references |
| `lyrics` | Plain/timed lyric candidates, language, timing format and rights/attribution |

Provider profiles may further specialize by media kind, language and artwork role. A new metadata domain can be added through a host contract. Until then, extensions may preserve data under their own namespace, but it cannot silently become a core sortable field or acquire UI/execution semantics.

Service names in examples are illustrative preferences. A subtitle provider only participates in episode descriptions/identity if it actually implements and declares those features. Installing a service does not imply support for all metadata types.

## Provider API

The metadata contract has four operations, with unsupported operations excluded at registration:

| Operation | Purpose |
| --- | --- |
| `identify(context)` | Return entity candidates and identity evidence/mappings |
| `enrich(entity, fields, locale)` | Return claims for specific requested fields of an established entity |
| `searchAssets(context, feature, filters, cursor)` | Find subtitles, lyrics or artwork for a host-managed choice |
| `resolveAsset(candidateRef, intent)` | Obtain an authorized expiring asset descriptor/download handle |

Requests carry a deadline, host job/context generation, required fields/languages, allowed identity projection and optional fingerprint handles. Unsupported media, unavailable input and missing authorization are explicit, never guessed around by reading local files. Pagination cursors are opaque and bound to the provider/account/query generation.

Example request projection:

```json
{
  "operation": "enrich",
  "requestId": "r-42",
  "contextGeneration": 17,
  "feature": "video.details",
  "target": "media-handle-7",
  "entity": {
    "kind": "episode",
    "titles": ["Example Show"],
    "externalIds": [{"namespace": "exampledb", "scope": "series", "value": "281"}],
    "episode": {"scheme": "seasonal", "season": 2, "episodes": [3]}
  },
  "fields": ["episodeTitle", "description"],
  "locale": "en-AU",
  "deadlineMs": 5000
}
```

An opaque target is echoed for correlation; it is not a readable path. It grants no access outside the operation. Actual job IDs and opaque handles are assigned and validated by the broker.

Every result contains a typed status, records/candidates, per-record identity, field claims, supplied match evidence, attribution/license information, fetched/expiry times and optional retry/cursor information. The host adds provider ID, package version/commit/digest and account/config generation itself. Provider “confidence” is evidence with provider-specific meaning; it is not a cross-provider probability or authority to override conflicts.

| Status | Coordinator response |
| --- | --- |
| `match` | Validate identity and claims; it may still fail host acceptance rules |
| `partial` | Accept eligible claims, then continue for missing required fields/languages |
| `no_match` | Continue to the next eligible provider; cache briefly for that exact query |
| `ambiguous` | Keep alternatives; require explicit choice where identity cannot be resolved |
| `auth_required` | Surface setup/reconnect; continue according to profile policy |
| `rate_limited` | Honor retry time, avoid retry storms, optionally continue to the next provider |
| `offline` | Use valid cache where allowed; do not label as no match |
| `unsupported` | Skip and record declared capability mismatch for diagnostics |
| `error` | Bound retries/backoff, preserve partial successes, continue if policy allows |

Only one terminal result is permitted per request. Streaming partial batches are bounded, ordered and followed by one terminal status; cancellation is host-owned. Results never mutate a live input item directly.

## Ranking and fallback algorithm

Store ordered contribution IDs in a profile keyed by feature, media kind and optional locale/account profile. For example, `video.details/episode` and `subtitles/episode/fr` can have entirely different orders. Missing/uninstalled providers stay visible as unavailable in saved preferences until removed; they do not shift other providers unpredictably. Initial catalogue order is a host default, not a priority number supplied by the provider.

For automatic metadata enrichment:

1. Collect permitted local evidence, user overrides and accepted metadata, then freeze the target revision and policy generation.
2. Determine requested fields and language requirements. Determine eligible registrations by supported contracts/media kind, enablement, grants, setup/account status and circuit-breaker state.
3. Visit the user's provider order sequentially. Coalesce identical in-flight requests and consult the relevant provider cache before spending network budget.
4. Validate the candidate's identity against host evidence. Reject known ID/type/year/episode conflicts. Weak or tied identities remain suggestions and cannot authorize automatic changes.
5. Accept claims only for the same established entity. Fill eligible missing fields; preserve already accepted higher-ranked claims and protected user/local selections. Retain conflicting alternatives with provenance for inspection.
6. After a partial result, ask the next provider only for remaining requirements. A lower provider may fill a missing description for a confirmed episode; it may not substitute a similarly named episode to fill that field.
7. Stop when required fields/languages and host quality rules are satisfied, or the total provider/page/time budget is exhausted. Optional fields can be resolved in a separate bounded job if the consumer requests them.
8. Apply a patch using target identity/revision and job generation. Recheck field-level revisions so a user edit made during lookup survives. Emit one host-owned metadata change event after commit.

“First nonempty response” is not a success condition. Nor is the old music-specific completeness rule “title + artist + album.” Completeness depends on the consumer's requested fields and media kind. A refusal, timeout or rate limit is not evidence that content does not exist.

Changing a provider profile or explicitly refreshing can reevaluate existing automatic claims under the new ordering. Such a refresh builds a new automatic layer and commits it with field-revision checks; it does not treat every old automatic value as permanently protected. Explicit user edits/selections and original observations remain intact. Ordinary view reopening may reuse valid accepted claims and cache rather than trigger a fresh ranking cycle.

Ambiguity in identity is a stop for automatic application, even if other providers produce plausible text. A later provider may add corroborating scoped IDs that resolve the ambiguity, but order alone cannot resolve it. Multiple records from one provider are subject to the same rules.

Example: Provider A confirms episode 2x03 and supplies its title, but no description or thumbnail. Provider B confirms the same scoped episode identity and supplies both missing fields. Accept the combination with per-field provenance. If B returns 2x04, reject those claims. If A merely matches a fuzzy title with no adequate episode evidence, do not apply its result automatically.

For explicit asset search, support two modes: **preferred providers**, which uses ranked fallback, and **search all enabled**, which aggregates a bounded set concurrently for user comparison. Automatic subtitle selection uses existing identity/release/language rules before provider preference. A high-ranked incompatible subtitle never beats a compatible lower-ranked subtitle. User selection is preserved when later results arrive.

## Field ownership and persistence

Separate four layers:

1. Explicit user edits, chosen art and manually attached tracks: protected until the user clears/replaces them.
2. Accepted local/embedded/sidecar facts and local artwork ownership: retain their provenance and domain policy; a reliable tag and a filename guess have different strength.
3. Accepted automatic provider claims: refreshable under the resolver's policy and field revision checks.
4. Unaccepted guesses, conflicts and candidates: available for a choice UI, not silently promoted to facts.

Do not encode “all local data always wins” as an identity rule: a filename guess can be weaker than a verified external ID. Do protect explicit corrections. Keep original observations even when a better claim becomes effective. Provider rank applies within permitted automatic choices, not above user intent or identity validation.

Persist only host-approved fields through a library/playback adapter. Record field origin, entity scope, matching explanation, provider release, retrieval time, locale and attribution. Large artwork/subtitle/lyric files live in managed asset storage; library fields reference them. Respect provider cache/display restrictions through explicit retention/attribution metadata. A provider unable to express permitted retention must not cause indefinite persistence by default.

Metadata updates do not modify media files or sidecars in v1. A future write-back API would need separate authority and an explicit user operation. Removing a provider does not erase user-accepted library metadata; disconnecting an account removes sensitive caches and credentials as described in [packages and updates](02-packages-and-updates.md).

## Caching, scheduling and privacy

Cache keys include feature/fields, normalized structured identity and relevant fingerprint, locale/language/numbering scheme, provider contribution and release digest, response schema, account partition, config generation and matching-policy version. Alias-equivalent requests can coalesce only after host normalization establishes equivalence. Never share personalized results or tokens across accounts/add-ons.

Positive, partial, ambiguous and negative results have distinct TTL policies. Timeouts/auth errors never become long-lived negative entries. Rate limits drive a provider/account circuit breaker and the UI can explain fallback. Offline stale-cache use is clearly labeled and disallowed where retention/auth constraints prohibit it. New config/account generations invalidate inappropriate cache reuse. A late result may populate an eligible cache, but cannot commit to a stale media/view generation.

The host applies aggregate deadlines and request/page/result limits, with interactive work ahead of background enrichment. Background scanning is opt-in, uses visible progress/cancel controls and honors core network/privacy settings. No local folder or full library is sent just because one item is playing. User-cleared metadata must not be immediately refetched against their preference.

Each consumer of a coalesced lookup has its own waiter, deadline, generation and apply authorization. Closing one screen revokes its waiter immediately; the shared provider job continues for eligible library/background consumers. Cancel underlying work only when no eligible waiters remain. A completion is independently validated and applied for each consumer.

## Subtitles, lyrics and artwork

Providers return asset candidates containing an entity/owner, role, language, format, compatibility evidence, attribution and a provider-local reference. Timed lyrics state the timing model and units. Subtitle candidates state language, forced/SDH flags, episode coverage and release/frame-rate evidence; a hash match stays tied to the file actually hashed. Artwork differentiates a poster from a backdrop or logo.

Search/preview produces drafts. Download on explicit selection/save, with a separate opt-in policy for automatic fetching/attachment. The host resolves expiring URLs, injects credentials where approved, validates redirects/format/size and extracts supported archives into managed storage. Effective asset destination policy is built by the host from the intersection of requested destinations, approved grants and the exact operation's intent; returning a descriptor never grants authority. Apply this rule to discovery artwork as well. Reject traversal, links, excessive entries/decompression and unsupported payloads. Parse subtitle/lyric data with bounded host parsers. Providers never choose an arbitrary destination path.

Attachment is a normal player/track-assignment command with target generation checks and user-visible selection. Existing local tracks remain intact. Subtitle synchronization offsets are attachment state, not an excuse to rewrite identity. No result guarantees timing compatibility solely from a work-title match.

## Service discovery contract

A `discovery.source` declares supported media kinds and operations:

| Operation | Contract |
| --- | --- |
| `roots()` | Return source entry points, categories or user subscriptions |
| `browse(nodeRef, cursor, filters)` | Return a bounded page of child nodes/items |
| `search(query, filters, cursor)` | Return a bounded page with source-scoped relevance |
| `resolve(itemRef, intent)` | Return authorized playable descriptors immediately before use |
| `subscribe(collectionRef, revision)` | Optional bounded change feed, active only under a subscription/session grant |
| `suggest(context, limit)` | Optional queue/playlist population proposals; not a mutation |

Nodes/items have stable provider-local IDs, a host-opaque item reference, kind, title, metadata identity evidence, artwork handles, availability, attribution and supported actions. IDs survive URL/token changes. Cursors bind to source/account/query/filter generation. A refresh or source removal cancels earlier pages; duplicates collapse by stable identity, not just matching display text.

Use the same host media list/card UI as the library. Discovery results can provide metadata claims through the coordinator's ingest contract, avoiding a second immediate fetch for data already supplied. Other metadata providers may enrich missing fields subject to the same privacy and ranking policy.

### Playable resolution

Saved playlists store `package/contribution + item ID + account reference + safe display snapshot`, not a short-lived signed URL. Resolve just before playback and, if necessary, refresh an expiring descriptor without duplicating the queue entry. Recheck account authorization and item availability. A missing add-on/account shows an unavailable item with reconnect/install/remove choices.

A playable descriptor contains an approved protocol/URL or broker stream handle, origin policy, opaque credential binding, expiry/refresh reference, media identity, live/seekable properties and permitted content metadata. The host constructs its effective protocol/destination policy by intersecting the provider's proposal with already approved grants and the exact Play/Enqueue intent; the returned policy is never self-authorizing. Refresh references undergo the same checks. V1 add-on URLs are public HTTPS only. Plain HTTP and private/LAN services require the future separate grants reserved in [the architecture](01-architecture.md), rather than broadening `network.http`. DRM, subscription entitlements and unsupported playback formats remain service/engine constraints; an add-on cannot declare them away.

V1 should expose a narrow HTTPS playback adapter and trusted built-in protocol adapters. The adapter must enforce the descriptor's scheme/origin policy through redirects, nested playlists, HLS/DASH segments and key requests, not merely validate the initial URL. Do not ship unrestricted stream resolution until the native input path can enforce that policy end to end. A returned URI must never enable local file reads, arbitrary VLC options, access-module selection or capture. Secret headers/URLs are not persisted in ordinary queue/history/log text.

### Queue and playlist population

Browsing, refreshing or receiving suggestions never changes playback. A host-rendered Play/Enqueue/Add to playlist action creates a gesture token bound to the selected source items and exact intent. Add-on code cannot mint or reuse it for unrelated actions.

Queue commands use stable entry IDs, an expected queue revision and an idempotency key. A transaction validates all requested operations and either commits once or returns a conflict. Insert relative to stable entry IDs, not a stale row number. Large source pages become bounded batches; resolve each item lazily near playback rather than opening every stream during enqueue. Saved playlist writes use the same rules and a selected playlist handle.

Continuous radio/“keep the queue filled” behavior requires an explicit session grant with limits, such as append-only, at most ten pending suggestions and no replacement of manually queued items. Provide pause/stop controls and provenance on inserted entries. Cancellation or disable revokes future writes. Existing queued items stay unless the user requests removal.

## Replacing existing metadata paths

The redesign's native parsing, file-fingerprint, subtitle matching and artwork ownership logic should remain host policy. Adapt the existing OpenSubtitles-specific HTTP implementation behind the provider contract, and do the same for the current TVMaze episode lookup. They can begin as trusted built-in providers while third-party equivalents exercise the public worker contract.

Route Qt screens, `PlayerController` background requests, preparser network stages and library enrichment into the same coordinator. Stop legacy remote Lua metadata/art traversal on the new path; retain only deliberately registered local/native readers. Do not run old and new automatic online systems concurrently. This concerns implementation ownership, not compatibility with old add-on packages, which is out of scope.

Before enabling the new path, acceptance tests must demonstrate one logical lookup across simultaneous screen/background consumers, no stale writes, per-feature fallback, preserved user edits and no legacy network calls bypassing grants.

## Complete NoMatch evidence

Use the [metadata completion guide](sdk/metadata-completion.md) for the exact
`no_match` result and optional HTTP 404/410 declaration. The host creates a receipt
only for one actual current invocation with complete host observations. A pure
provider can make zero host calls. Pending, failed, canceled, partial, or malformed
work cannot certify NoMatch. This receipt alone does not authorize an aggregate
result or a metadata write.
