# Native external preparser contract

The external preparser uses the internal request owner. Its private parse
engine runs on the native parser executor. The public request handle remains
the internal handle. Submit, cancellation, final admission, sidebands, and
`on_ended` use that one request lifecycle.

## Child admission

A fetch-only request stays in the parent. A Parse request sends the complete
issued native baseline to the child. Both child configuration and per-request
options exclude metadata fetch flags. Only the parent selects local fetch,
native embedded artwork, legacy provider families, and owned remote stages.

Before child admission, the parent refuses an item policy, inherited object
policy, or raw add-on source URI. Revoked policy also refuses admission. The
parent does not remove a policy to obtain an unscoped child. Child pools start
only after this gate and a current, uncancelled request pass.

The baseline includes metadata priorities, extra tags, tracks, links, native
info groups, EPG tables and events, current indexes, EPG time, and statistics.
It excludes policy, private artwork records, backend state, grants, revision
tokens, and opaque owner data. Ordered options travel in the separate native
request field. The child applies folder defaults before media options.

Native output returns to the exact issued private parent item. Its URI,
options, policy, current links, and link selection state stay under native
control. The existing final PrepareLocal and Apply rules preserve target
fields and live statistics. The child never updates the public target. The parent prepares all native
allocations and link tables before it swaps facts into that private item.
It does not use the generic metadata merge, which can allocate during writes.

## Transport and lifetime

Each Parse child stops and reaps before its completion callbacks. The native
pool worker remains available. It starts its next child only for its next
admitted task. Thumbnail transport retains its existing reuse behavior.

The engine owns a heap completion state and holds its child task through
interrupt removal. Native cancellation interrupts the transport. Timeout and
cancellation wait for native transport completion and required reap. They do
not require a Qt event. Queue removal, callbacks, and task release occur outside
the pool lock. Failed transport cannot enter the parent fetch stages.

A pipeline entry holds the native request key until its last reference ends.
This includes a cancellation callback that overlaps provider completion.
Key release runs outside pipeline locks. A canceled local stage cannot access
a request that its terminal callback has already released.

Call preparser Delete outside every callback from that preparser. This rule
also applies to queued cancellation callbacks on the caller thread. Request
release and Cancel remain safe in callbacks. Delete closes the internal owner
before it waits for native workers or deletes child pools.

## Bounds

- A parent Parse transport reads at most 256 MiB before it refuses the response.
- New native facts have a 16 MiB allocation budget per item. New collections
  have at most 4096 entries. Info entries and EPG events also have aggregate
  limits of 4096 per item.
- Subtree output has at most 4096 nodes and 32 levels across all callback trees.
- Attachments have at most 4096 entries and 16 MiB of data and text in total.
- The JSON reader refuses structural depth above 128 before recursive parsing.
- Native 64-bit statistics and EPG times remain exact. Float statistics use
  their native 32-bit representation, including negative zero and NaN.
- A size, type, index, or allocation refusal cannot publish partial output.

## Evidence at source freeze

The controlled internal request fixture passes 11 groups and 21 scenarios,
both normally and with ASAN/UBSAN and leak diagnostics. Its injected native
engine tests successful admission and cancellation with callback reentry.
The retained-key fixture passes two cancellation race scenarios in both modes.
The native import fixture passes two groups in both modes. It tests each
allocation failure before complete success, URI mismatch, complete native
output, and retention of the current link selections and execution options.
This fixture owns the real item and metadata allocator, as the existing native
limit fixture does. Its unused external request routing must abort if called.

The real JSON serializer closure passes six groups and 17 scenarios in both
modes. It tests baseline and result round trips, native info/EPG/statistics,
metadata priorities, multiple trees, attachments, invalid scalars and indexes,
collection and depth limits, and interrupted I/O. An active native interrupt
must stop EINTR retry; an ordinary EINTR can retry.

The normal Core, JSON serializer module, and both child programs build with
the registered source. All 26 affected normal test targets pass. The real
internal/external WAV and playlist fixture passes 16 cases with absent, active,
closed, and suspended owners. The folder fixture passes 14 cases, and subtree
identity passes two cases. The restored comparison also passes with 999 audio,
999 video, and 999 subtitle tracks per item. Its timeout uses the valid native
value zero.

The new owned-pipeline fixture passes nine cases through the real child. Five
cases prove fetch-only work needs no child binary and scoped, inherited,
revoked, and raw source inputs cannot enter the child. Four loopback HTTP cases
prove native read cancellation, owner close, timeout, and target URI ABA
refusal. Their terminal statuses are EINTR, EINTR, ETIMEDOUT, and EAGAIN.
Delete completes before the blocked server can proceed. No Qt event or public
service key enters these tests.

The first loopback run failed before it could connect. The fixture had set an
empty proxy environment value, which the native POSIX helper treated as an
invalid proxy. The corrected fixture removes those environment variables and
supplies the trusted native item proxy option. All nine cases then pass. The
native HTTP policy stays unchanged.

Native link priorities USER, FOLDER_RULE, and ML_AUTO round-trip in both the
baseline and response fixtures. The decoder still refuses values beyond the
native enum. The final 17-scenario IPC fixture also passes ASAN/UBSAN and leak
diagnostics. All allocation and cancellation fixture results remain separate
from these real module tests.

The owner activation gate remains closed. Native provider files remain in
place. Identity choice and full native artwork parity remain separate gates.
