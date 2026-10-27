# Prepared native parse output

The native delta and request guard are available in Core. The internal
preparser mount now uses them. The external parent mount remains a separate
stage. The native provider families remain in place. The MainUI metadata owner
stays inactive for Core parse requests until consumer and provider parity pass.

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

The exact sealed native artwork claim can retain the ordinary native route.
This rule preserves native local/cache, attachment, and embedded artwork output.
It applies only when the original item has no native policy or source marker.
It creates no approval record. A provider cannot change the sealed claim or
gain this route through a matching field name. Provider artwork still needs
its own current native approval. Scoped media cannot use this exception.

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

`test/src/misc/metadata_local.c` has 11 groups and 32 scenarios. It covers
native output parity, priority retention, exact policy, link selection,
remote-result isolation, sideband admission, same-value and ABA edits, live
progress, cancellation, and the standard-only path. It also covers sealed
ordinary-native artwork and refusal of scoped or changed provider claims.

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
from the fresh shared Core. The current fixture passes all 11 groups and 32
scenarios through that Core. The isolated source fixture also passes normally
and with sanitizer diagnostics. The normal limit, commit, pipeline, fetcher,
and token-limit targets pass with the artwork exception.
It requires compiler symbol aliases, function sections, and linker section GC.

The limit fixture includes the real policy source. Its item object and its
metadata/backend source use one executable-owned token sequence. The unused
directory option parser has an abort-only test guard. A test cannot pass if it
calls that guard. This two-object closure passes all six limit groups against
the fresh Core library. It requires no new production export or fault hook.

## Internal request mount

The internal preparser submits each parse or fetch request through
`vlc_metadata_pipeline_PushRequest`. A retained local anchor precedes native
work. `CopyLocalItem` supplies the parser's exact private item. Parser subtrees
and attachments enter the bounded request collector. They cannot reach a
public callback before final admission.

The native front stage first uses local readers and caches with
`META_FETCHER_PROVIDER_NONE`. If the initial owner decision was never-owned
`VLC_ENOENT`, a separate legacy artwork copy can use the native lease. The
four shipped provider families remain available through that path. An owner
or consent transition discards that copy and keeps the native local result.
An owned, closed, or suspended profile never acquires that lease. Local output
remains available with network access disabled.

The parent seals local facts and sidebands after native I/O ends. An owned
backend can then run its native artwork and ranked remote stages. One final
request guard admits the full result and takes its sidebands under the same
lock. A late cancellation cannot relabel the admitted result or remove those
sidebands. Public callbacks run outside this lock. Each subtree root binds to
the original request item before its callback.

The request stays on the parent list through `on_ended`. A separate provider
reference retains its request and parent state until native I/O ends. Cancel
covers queued parse work, active parse work, queued or active fetch work, and
owned remote work. Parent Delete closes the pipeline before native executor
waits. A native callback never needs a Qt event to release its request.

The controlled internal fixture uses the real request, fetcher, pipeline,
backend, snapshot, delta, and final Apply code. Its native parser, module I/O,
and executor schedule are controlled. Its unused thumbnail entry points
abort. The thin item-source closure supplies the real private status setters;
shared Core owns the metadata token sequence. Eleven groups and 21 scenarios
pass normally and with ASAN/UBSAN and leak diagnostics. Native file and
attachment results retain their fetched status without a downloader. The
existing native shutdown fixture also passes 60 cases with real executors.
This evidence does not prove file-format parity.

The separate application fixture uses the public preparser API and real
native WAV and playlist modules. It tests tracks, duration, complete baseline
retention, subtree root identity, and local output with absent, closed, and
suspended owners. Its eight cases pass through the fresh normal Core. All nine
affected normal target groups pass, including the prewrite query, controlled
internal request fixture, legacy artwork fallback, native shutdown, pipeline,
fetcher, local-result, and standard commit tests.

Request release and cancellation retain their callback-safe behavior. Call
preparser Delete outside every callback from that preparser. This rule includes
queued cancellation callbacks on the caller thread. The owner must schedule
Delete after its callback returns. A synchronous Delete from an executor
callback can wait on that same executor.

## Native prewrite query

`vlc_metadata_job_IsCurrentFor` reads the exact native target under the backend
and item locks. It has no allocation, I/O, callback, or token reservation. It
accepts pending and retained-complete successful local anchors before their
one Apply attempt. It refuses cancellation, a closed or suspended backend,
changed identity, extra tags, result-field tokens, proposed native facts, or
URI/name/option revisions. It also refuses changed or revoked policy. Live
statistics, EPG time, and unrelated metadata retain the Apply rules.

A native artwork receipt can use this query before a tag-file write. The query
grants no write or approval authority. It cannot guarantee that a later commit
will pass. The final Apply must validate again. Repeated queries do not consume
the anchor. The five-group native query fixture has 17 scenarios.

## Remaining mount gates

- The external parent now uses the full native snapshot and the same request
  owner. Its child carries no fetch flags. Fetch-only work stays in the parent.
  The [external preparser contract](native-external-preparser-contract.md)
  records the actual child, cancellation, and parity tests.
- Complete native artwork format parity and the native embedded HTTP/LAN asset
  adapter before global owner activation. Keep every native provider family
  until its actual consumer replacement passes.
- Mount the host identity-choice store through field-revision admission. The
  old untracked metadata-choice draft remains inactive.
