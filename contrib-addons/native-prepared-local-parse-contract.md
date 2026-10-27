# Prepared native parse output

This stage is a prerequisite for the owned preparser route. It does not mount
that route. The native provider families and the existing preparser remain in
place. The MainUI metadata owner stays inactive for Core parse requests.

The host can use these private Core functions:

1. Call `vlc_metadata_job_New` before local work. Start a local anchor with
   `vlc_metadata_job_StartLocal`.
2. Call `vlc_metadata_job_CopyLocalItem` once. This function returns a complete
   private native snapshot. It retains information groups, EPG, statistics,
   execution options, extra-tag priorities, and the current native policy.
   Ordinary `input_item_Copy` keeps its existing behavior.
3. Run the native parser on this exact private item. Keep parser callbacks off
   the live target. Collect subtree and attachment output.
4. Call `vlc_metadata_job_PrepareLocal` before a remote stage. This function
   seals a separate native delta. A remote result cannot change that delta.
   It copies subtree nodes and their native items. It holds immutable native
   attachment references. The caller retains the supplied objects.
5. Complete the standard metadata result. Call `vlc_metadata_job_Apply` at the
   final parent admission point. A failed admission changes no target field.
6. After successful admission, call `vlc_metadata_job_TakeLocalEvents` once.
   Deliver the native callbacks outside all admission locks. Then call
   `vlc_metadata_local_events_Clear`.

No worker operation can use this API. A local delta requires the exact item
that the anchor issued. A URI or option change on that item refuses the delta.
The host must keep attachment objects immutable until their last release.

## Commit rules

The native delta can publish duration, track formats and stable IDs, information
groups, extra tags and their priorities, native item type, EPG, metadata status,
and new track links. A native name can fill an absent name only. Standard fields
keep the existing missing-field and explicit native replacement-mask rules.

URI, execution options, opaque owner data, network classification, and policy
are not output fields. The target keeps its current link objects and selection
flags. New links append by URI; a duplicate URI cannot replace a current link.

Each native fact group has a private revision token. The commit compares the
original URI, exact policy, identity metadata tokens, extra-tag token, URI/name/
option tokens, and every proposed nontransient native fact token. Same-value
writes and ABA writes advance these tokens. An exhausted proposed token refuses
the complete commit. Under the target lock, the host admits all native token
increments and reserves all standard, extra-tag, and status tokens before any
write. The existing final policy-revocation read remains the linearization
point. No allocation or callback occurs in the write phase.

Live progress has precedence. Current statistics remain intact. Parser
statistics can fill an absent statistics object. Current EPG time remains
intact; parser time can fill zero. A concurrent EPG table change preserves that
live table and does not block the other metadata fields. A concurrent metadata
status change also preserves the current status. With an unchanged status
token, the commit adds the prepared status bits through the existing OR rule.

Cancel, automatic consent withdrawal, or backend Close before admission refuses
all output. A successful local anchor retains its existing bounded slot until
Apply, Cancel, or Close. Apply still has one attempt. Cancel after admission
does not undo an admitted result or its sideband output.

## Resource limits

Each complete native snapshot has a 16 MiB copy budget. Each collection has at
most 4096 entries. The complete subtree collection has a separate 16 MiB copy
budget, at most 4096 nodes, and at most 32 tree levels. Retained attachment data
has a separate 16 MiB limit. No incomplete allocation becomes a prepared delta.
The native backend retains its existing 1024-job bound. These limits apply
before a future preparser mount; they do not narrow the current preparser path.

## Verification

`test/src/misc/metadata_local.c` has nine groups and 25 scenarios. It covers
native output parity, priority retention, exact policy, link selection,
remote-result isolation, sideband admission, same-value and ABA edits, live
progress, cancellation, and the standard-only path.

`test/src/misc/metadata_local_limit.c` has six groups. It injects each allocation
failure in native snapshot and delta preparation, without a production fault
API. It also covers token exhaustion, cancellation during value preparation,
attachment and tree limits, and the copy byte limit. The existing eight-group
`metadata_commit.c` suite covers the standard field path.

The isolated recipe uses the normal Core compiler flags from
`build-redesign/compile_commands.json`. It compiles the current item, metadata,
policy, artwork-proof, native-copy, and backend sources into a private archive.
The test object links that archive with the existing Core library. It renames
unused filename and metadata-writer entry points. A fail-fast test-only stub
guards the unused directory option parser. This recipe does not prove the
future parser mount. The normal Core build and all 22 affected native test groups now pass.

The thin shared-Core fixture uses `metadata_local_setters.c`. This file includes
the real item source and aliases unused definitions before section GC. Only
the three private EPG/time/status setters remain as local definitions. The
constructor, ordinary copy, metadata allocator, and all delta operations come
from the fresh shared Core. This fixture passes all nine groups and 25 scenarios.
It requires compiler symbol aliases, function sections, and linker section GC.

The limit fixture includes the real policy source. Its item object and its
metadata/backend source use one executable-owned token sequence. The unused
directory option parser has an abort-only test guard. A test cannot pass if it
calls that guard. This two-object closure passes all six limit groups against
the fresh Core library. It requires no new production export or fault hook.

## Remaining mount gates

- Use the full native snapshot at both parent preparser entries. Keep the remote
  stage visible to parent cancellation, including the external child handoff.
- Preserve local and offline output when the owner is absent, closed, or
  suspended. Only the never-owned `VLC_ENOENT` result permits a legacy remote
  lease. A retired owner cannot restore directory-scanned remote providers.
- Add a trusted local-reader selector before full Parse executes Lua metadata
  readers. `ReadMeta` still scans `meta/reader` with a NULL provider context.
  The owned-local fetcher selector does not cover that path. Preserve shipped
  local readers and filename behavior.
- Complete native artwork format parity and the native embedded HTTP/LAN asset
  adapter before global owner activation. Keep every native provider family
  until its actual consumer replacement passes.
- Mount the host identity-choice store through field-revision admission. The
  old untracked metadata-choice draft remains inactive.
