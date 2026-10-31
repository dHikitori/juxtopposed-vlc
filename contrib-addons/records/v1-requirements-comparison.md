# v1 requirements comparison and text audit (Step 7)

Step 7 of the [continuation plan](../07-continuation-plan.md), last item.
Written 31 October 2026, after Step 6 and the Step 7 tools. This compares
each section of the design documents `01`–`05` with the implementation. The
evidence for each mounted part is in [the maturity matrix](../IMPLEMENTATION.md);
this record lists what is open.

## Summary

The maturity matrix has 47 requirement rows: 38 mounted, 8 component-tested
(3 wait for the native metadata owner to be on by default, 5 for a full-app
or real-device run) and 1 designed (the optional signed curated install
path).

| Document | Sections | Status |
| --- | --- | --- |
| [01 architecture](../01-architecture.md) | 8 | All mounted. Open: a published support window and an oldest/newest host test (versioning). |
| [02 packages and updates](../02-packages-and-updates.md) | 9 | Mounted except the optional signed catalogue. Open: the "auto compatible" update policy and an update-check schedule; a host revocation list for compromised releases; settings and private-data migration on update with a commit journal (install steps 6 and 7). |
| [03 providers and discovery](../03-providers-and-discovery.md) | 12 | Mounted, with the native owner component-tested and off by default. Open: owner on by default; Core and media-library writes that check retention `keep`; lyrics (no provider or consumer); `queue.apply` proposals. |
| [04 UI, themes and configuration](../04-ui-themes-and-configuration.md) | 10 | Mounted. Open: host shortcut assignment for add-on commands; account-scoped setting values and PKCE; an interactive preview with keyboard and narrow-window checks (the preview tool writes images). |
| [05 visualizers and sessions](../05-visualizers-and-sessions.md) | 10 | Mounted except session authority, WSS and drift, which are component-tested. Open: full-app session link; rate convergence on a real audio output and two machines; GPU-loss fallback on real hardware. |

## Open items by section

### 01 Architecture

- **Versioning and compatibility.** Contract ranges, unknown-field refusal
  and extension namespaces are enforced (`Manifest::parse`, now with the
  manifest schema). There is no published support window and no test
  against the oldest supported host.

### 02 Packages and updates

- **Catalogue and direct-source trust.** The signed curated path is
  designed only (optional for v1).
- **Install and update transaction.** Steps 5 and 8 (offline check, health
  period) are mounted. Steps 6 and 7 (settings and private-data migration
  into a staged data generation, commit journal) are open. The health
  period does not survive a restart.
- **Update policies.** Pinned and notify-compatible checks exist
  (`checkRepositoryUpdates`). Auto-compatible activation and a scheduled
  check with jitter and backoff do not.
- **Rollback, disable and uninstall.** Mounted. There is no host-owned
  revocation list; quarantine is from crashes only.
- **Author workflow.** Validator, schemas, templates, preview, generated
  types and fixture tests exist. There is no mocked-broker replay tool for
  authors outside the test suites.

### 03 Providers and discovery

- **Field ownership and persistence.** Retention `keep` is enforced for the
  episode-title library adapter; Core Apply and the media library do not
  check it for portable fields (the native owner is off by default).
- **Subtitles, lyrics and artwork.** Subtitles and artwork are mounted.
  Lyrics have no provider contract consumer.
- **Queue and playlist population.** Reads, append through gestures and
  queue filling are mounted; guest `queue.apply` proposals are open.
- **Replacing existing metadata paths.** Parity is component-tested for the
  four artwork families; the owner stays off by default until the Windows
  live checks pass.

### 04 UI, themes and configuration

- **Player controls and customization.** `player.actions` commands are
  mounted. Host shortcut assignment for add-on commands is not.
- **Generated configuration.** Profile values and session values are
  mounted; account-scoped values are open.
- **Accounts.** Device-code sign-in is mounted; authorization code with PKCE
  is open. No live sign-in service run.
- **UI conformance.** Theme switching, plain text, focus and recovery have
  QML tests. RTL, enlarged text and screen-reader checks are not automated.

### 05 Visualizers and sessions

- **Audio analysis and rendering.** GPU-loss fallback is tested with a
  simulated loss only.
- **Watch with friends.** Leases, arbitration, local priority and WSS are
  component-tested; the full app link is open. The drift experiment shows
  about 50 ms seek alignment on one machine and an admitted, restored
  bounded rate; rate convergence needs a real audio output
  ([experiment](session-drift-experiment.md)).
- **Cross-cutting scenarios.** The eight scenarios have component tests;
  "add-on update during a room session" depends on the open update
  transaction steps.

## Platforms

- **Linux:** every label passes (see the matrix).
- **Windows:** the user's live runs covered start, close and the Step 5
  repairs. Step 6 and Step 7 changes are in the package built after this
  record, for the final Windows check.
- **macOS:** no build. This environment has no macOS host or SDK, so the
  first build, the Keychain credential store, the fence, the socket, the
  worker sandbox and an app run are all open.

## Text audit (ASD-STE100)

`contrib-addons/check-ste.py` screens the 753 translatable strings of the
add-on host and its screens. The first run found 58 items; 33 strings were
rewritten, and 22 findings remain: -ing nouns ("Saving your choice…" → "The app saves your
choice…"), "being" constructions, "has been reached" passives, "requires"
→ "needs", "Setup required" → "Set up first". The remaining findings are reviewed:

- State descriptions with a past participle used as an adjective ("The
  account is connected.", "No extensions are installed.").
- "Allow" on permission buttons, which is the platform term users know.
- "binding" as the manifest term `credentials.use` bindings.
- Two error passives where the actor is the host policy ("properties that
  are not permitted").

The SDK and design documents were written to the same rules. A person
should still read new text; the checker finds only mechanical problems.
