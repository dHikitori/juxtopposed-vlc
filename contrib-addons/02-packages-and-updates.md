# Packages, repositories and updates

## Repository layout

An installable release is a tag in an HTTPS Git repository containing a complete package. GitHub, GitLab and self-hosted Git use the same source model. A catalogue is optional discovery and trust infrastructure, not a mandatory hosting service.

```text
addon.json
LICENSE
dist/addon.wasm             # optional executable logic, built by the author
themes/midnight.json        # optional theme
views/radio-browser.json    # optional host-rendered view
settings/schema.json        # optional configuration data schema
settings/ui.json            # optional form layout hints
locales/en.json
assets/icon.png
README.md
```

Only declared package files are installed. V1 uses one package at the repository root; a monorepo can publish a dedicated package repository. Package contents must be self-contained. No submodules, Git LFS downloads, symlinks, external imports, dependency installation or post-install scripts. Authors commit built portable artifacts at the tag; users do not need Rust, npm, a compiler or Git command-line tools to install an add-on.

The application supplies a restricted Git transport implementation. Its library choice is an implementation decision. It must not inherit system/user Git helpers, hooks, aliases, credential commands, filters or repository-local execution configuration. Do not run an ordinary user-configured checkout on a downloaded repository. HTTPS is the public v1 transport. Private repositories can use a host-managed read credential flow; SSH transports and local working directories belong to a later explicit developer/private-source profile.

## Example manifest

This is a proposed, illustrative manifest, not a schema consumed by today's code. The reserved `.example` service does not exist. The package implements subtitle search only; supporting movie/episode metadata would require additional declared features and their contracts.

```json
{
  "formatVersion": 1,
  "id": "org.example.subtitles",
  "name": "Example Subtitles",
  "version": "1.0.0",
  "publisher": "Example Community",
  "license": "GPL-2.0-or-later",
  "source": "https://github.com/hakone-org/example-subtitles-addon.git",
  "api": {
    "addon": ">=1.0.0 <2.0.0",
    "contracts": {
      "metadata": "^1.0.0",
      "settings": "^1.0.0"
    }
  },
  "runtime": {
    "kind": "wasm-message-v1",
    "entry": "dist/addon.wasm"
  },
  "files": [
    "addon.json",
    "LICENSE",
    "dist/addon.wasm",
    "settings/schema.json",
    "settings/ui.json",
    "locales/en.json",
    "assets/icon.png"
  ],
  "contributions": [
    {
      "id": "search",
      "type": "metadata.provider",
      "contract": "metadata@1",
      "features": ["subtitles"],
      "mediaKinds": ["movie", "episode", "video"],
      "operations": ["searchAssets", "resolveAsset"],
      "requires": ["metadata.context", "network.http", "credentials.use"]
    }
  ],
  "permissions": {
    "required": {
      "metadata.context": {
        "fields": ["kind", "titles", "year", "externalIds", "episode", "languages"]
      },
      "network.http": {
        "origins": ["https://api.subtitles.example", "https://downloads.subtitles.example"]
      },
      "credentials.use": {
        "bindings": [
          {
            "field": "apiKey",
            "origin": "https://api.subtitles.example",
            "header": "Api-Key"
          }
        ]
      }
    },
    "optional": {
      "media.fingerprint": {
        "algorithms": ["opensubtitles-v1"]
      }
    }
  },
  "settings": {
    "version": 1,
    "schema": "settings/schema.json",
    "ui": "settings/ui.json"
  },
  "locales": {
    "default": "en",
    "files": {"en": "locales/en.json"}
  },
  "icon": "assets/icon.png"
}
```

The optional checksum permission improves matching; denying it leaves title/ID lookup available. Host-provided default grants only cover package resource reads, own nonsecret settings, bounded logging and dispatch. Everything in `requires` must be declared and granted before that contribution activates. Optional grants are tested at runtime. Host protocol requirements such as `ui@1` or `visualizer@1` would appear in `api.contracts` when used.

## Manifest rules

| Field | Proposed rule |
| --- | --- |
| `formatVersion` | Required integer; unsupported major formats rejected before executable content is loaded |
| `id` | Required stable lowercase reverse-domain-style name; syntax is not proof of domain ownership |
| `name`, `publisher`, `license` | Required display and licensing metadata; catalogue badge/trust status comes from the host, not these strings |
| `version` | Required SemVer release; `v1.0.0` is the conventional tag for manifest version `1.0.0` |
| `source` | Required canonical source; must match approved install source, rather than redirecting updates itself |
| `api` | Required add-on API range and required feature contract ranges; optional contracts must be separately marked with dependent contribution IDs |
| `runtime` | Omitted for data-only packages; otherwise one supported runtime kind and package-local entry |
| `files` | Required exact portable paths including manifest, resources, locales and license; no globs or runtime remote code |
| `contributions` | Required nonempty list; local IDs unique, exported as `packageId/localId` |
| `permissions` | Empty if unused; explicit required/optional scopes, network origins and credential bindings |
| `settings` | Optional schema/form paths and integer settings version; migration chain required for incompatible saved-data changes |
| `locales`, `icon` | Optional validated resource paths, with a fallback language and host icon fallback |

Unknown top-level fields fail validation, except a documented `extensions` namespace. Resource paths must be relative, normalized and portable; reject `..`, absolute paths, drive letters, alternate streams, reserved device names, case-fold/Unicode-normalization collisions and links. Limit repository transfer, history depth, object count, file count, per-file size and unpacked package bytes. Parsing/validation itself runs under budgets. The host validates permissions against actual contribution contracts; a declaration cannot invent an unrestricted capability.

Before SDK v1, the complete schema must also freeze syntax for optional contracts and their dependent contributions, migration chains/private-data versions, offline self-test entrypoints and per-permission denial/degradation behavior. The example above intentionally exercises only the simpler required-contract case. These are required schema deliverables, not features that arbitrary extra fields can already activate.

No cross-add-on runtime dependencies in v1. An author bundles dependencies and license notices into the release. Optional integration uses negotiated host contracts, not importing another add-on's internal files or mutable global state. This avoids dependency cycles, hidden install-time code and coordinated rollback requirements. Multi-contribution packages can share one worker and private state.

## Identity and the installation lock

A tag is a human-readable release selection, not immutable content. Git explicitly supports [replacing tags with force](https://git-scm.com/docs/git-tag.html). On first resolution, record both the advertised tag object ID (if annotated) and the peeled commit object ID. Also compute a SHA-256 package digest over a canonical sorted map of every declared path, length and file-content hash, including the manifest. Define that canonical encoding in the implementation; do not use a platform-dependent archive timestamp or file order.

The host-owned installation lock stores:

```text
package ID and declared version
approved source URL and repository identity
tag name + tag object ID, when annotated + peeled commit ID
Git object hash algorithm + independent SHA-256 package digest
publisher/catalogue trust anchor and verification result, if any
selected update policy and compatible version range
negotiated contracts and required host capabilities
granted permissions + consent revision
settings/data schema versions + active storage generation
active release pointer + retained previous release
```

The lock is not supplied by the add-on and cannot be edited by its worker. Never update a source merely because a manifest requests a new URL. Repository transfer, mirror switch, add-on ID collision and publisher-key changes are separate trust decisions. A preexisting package with the same ID from another source must not be overwritten silently.

## Catalogue and direct-source trust

Support two install flows:

1. A curated catalogue maps package ID, source, release tag, commit, digest and approved publisher key through signed catalogue metadata. Verify those mappings independently of the repository. Cache expiry/revocation policy and key rotation belong to the catalogue implementation.
2. Direct Git installation accepts a repository URL and tag, displays the resolved identity, requested authority and whether publisher verification is absent, and records that source as the trust anchor for later updates. It is not displayed as catalogue-verified. Signed tags can strengthen publisher continuity only after the user/catalogue trusts the signing key; an unknown self-signed key does not establish identity.

The catalogue must not be able to enlarge permissions without the ordinary update consent step. Integrity, publisher identity, catalogue review and user grants are separate signals. Release signatures are recommended for direct sources and required by the curated catalogue policy proposed here. Unsigned direct sources remain possible with explicit initial consent.

## Install and update transaction

1. Resolve source/tag without executing repository content. Fetch the minimal objects needed, with network and size limits.
2. Verify advertised IDs, trusted signatures/catalogue mapping where applicable, complete file inventory, content digest, manifest and API compatibility. Reject changed content for a previously locked tag/version.
3. Stage immutable package contents in application data, outside the source tree. The installed release is a read-only snapshot, not a live Git checkout.
4. Compare requested authority and trust identity with existing grants. Show new sensitive scopes/origins as a consent delta. If consent is pending, keep the old version active.
5. Validate theme/view/settings documents. Run a bounded offline self-test if declared; no network availability or secret access is needed to pass installation.
6. Quiesce the old worker and drain/cancel pending effects. Snapshot settings/private data at a consistent revision, then run deterministic migrations on a staging copy with no network, playback or general storage imports.
7. Prepare the new worker and complete contract negotiation under the staged grant set. Atomically commit the release pointer, data generation and contribution registry generation. A commit journal recovers an interrupted switch on the next start.
8. Activate new contributions and run a bounded health observation period. Retain the previous release and its matching settings/data snapshot. On failure, restore the old coherent generation and the host's built-in UI where needed.

No two versions may simultaneously own a playback/session lease or mutate the active storage namespace. In-flight remote server effects, token rotations and messages already sent cannot be rolled back; update logic must not claim that local transactions undo them. Quiesce collaborative sessions explicitly before switching versions; do not attempt to serialize a live Wasm stack or socket into another release.

Settings migrations are version-to-version pure transforms on structured data. They must be deterministic, bounded, testable and reject invalid output. Private application data migrations use a staged namespace and a declared schema chain. If required migration steps are missing, leave the old release active. Never run arbitrary install scripts or silently clear configuration to make an update succeed.

Migrations receive only opaque credential references/status, cannot inspect or duplicate secret material and cannot retarget a reference to a new account/origin/header. The broker validates retained references against the new manifest/grants. Credential updates/deletions are coordinated with the settings/data generation transaction; a failed activation must not prematurely delete the active release's usable credential. External token rotation remains an external effect that may require reconnect rather than local rollback.

## Update policies

| User-selected policy | Behavior |
| --- | --- |
| Pinned tag, default | Stay on exact locked release; explicit new version selection required |
| Notify compatible | Check SemVer tags against the chosen range, host contracts and trust; offer an update |
| Auto compatible | Stage and activate compatible releases with unchanged approved authority; pause for changed permissions/trust |
| Developer source | Explicit local/branch workflow, separately labeled, no production auto-update assumptions |

Tag enumeration is ordinary Git behavior, not a GitHub Releases dependency. Release assets/APIs can later optimize transport but must map to the same locked contents. Do not compare tags lexicographically. For normal releases, the SemVer tag after stripping one optional leading `v` must equal the manifest version; reject disagreement before install/update. Ignore unrelated/non-SemVer tags during automatic selection; prereleases require a user-selected prerelease policy. “Compatible” considers contract ranges and platform profile, not package version alone. Direct development pins to non-SemVer refs remain separately labeled and outside automatic release selection.

If a tag moves, do not refresh its lock or silently accept its new digest. Explain the mismatch and require a new release tag or explicit source reapproval. If a newer candidate is invalid, retain the installed version and show the candidate's failure. Bound tag enumeration and retain negative verification results briefly to avoid update loops.

Use an opt-in update check schedule with cached results, jitter and exponential backoff. Offline starts always use installed contents. A removed repository cannot disable an otherwise valid installed package. A host-owned revocation list may quarantine a known compromised release; recovery still uses verified local contents and built-in fallbacks, never an unverified emergency download.

## Rollback, disable and uninstall

Rollback selects a retained immutable release with its matching data snapshot; it does not run a reverse migration. If the user changed settings/data after updating, explain that the older snapshot will be restored and retain the newer one separately for recovery. Do not feed newer-schema state into old code. Offer an explicit export of nonsecret settings where useful; secrets are never exported in ordinary configuration.

Disable revokes active grants/leases, cancels jobs and withdraws contributions while preserving settings. Uninstall removes executable/resources and source-specific registrations. Ask whether to retain configuration/account connections; clearly expose a full data-removal option. Account disconnect deletes tokens and account-scoped caches. User-accepted metadata already committed to the library stays with provenance where its retention/account terms permit; independently authored user corrections remain. Cache-only results can be removed separately. Saved playlists preserve source references and show unavailable items instead of silently substituting another service.

## Author workflow to build later

Provide an SDK with manifest/schema validation, contribution templates, a mocked broker, fixture replay, local developer install and package inspection. A command-line validator should check resources, permissions, contract versions, localization, configuration migrations and portable Wasm imports before the author tags a release. A UI preview host should exercise dark/light themes, scaling, keyboard navigation and narrow windows.

V1 can use committed build artifacts and signed tags. Build attestations, isolated CI packaging and catalogue review improve distribution later. None require an install-time build on a user's computer. All example manifests and generated settings must ship with SDK conformance tests before the package format is declared stable.
