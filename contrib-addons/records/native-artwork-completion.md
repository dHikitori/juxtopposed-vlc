# Native artwork completion evidence

The native provider path can now retain a closed lookup outcome for one
exact job, snapshot and provider family. This is the evidence layer for the
later ranked not-found transaction. It does not set `ITEM_ART_NOTFOUND`.
The app metadata owner remains inactive until that transaction and provider
parity are complete.

## Result and lifetime

The named finder reports `UNKNOWN`, `MATCH`, `NO_MATCH`, `NOT_APPLICABLE`,
`UNAVAILABLE`, `DENIED` or `ERROR`. The four app-shipped Lua artwork scripts
retain their first URL or nil return. A second return supplies the outcome.
The closed decoder refuses inconsistent types and values.

`vlc_metadata_native_provider_ProbeOutcome` supplies an immutable native
receipt. Its callback signature stays the same. `claim_GetOutcome` reads the
outcome. Only `MATCH` can contain an artwork address. The old `Probe` keeps
its positive-only receipt behavior.

The callback borrows the receipt. A retained receipt occupies one of the
32 native admission slots until its final release. Cancel, Close, a stale
native snapshot or owner retirement makes it unusable. Delete joins active
work. Closed retained receipts can be released after Delete.

## Complete input

The lookup attaches a private native observer before access or filter code
runs. Child objects inherit the retained scope. Each stream has a retained
observer reference before its constructor can read or start a worker.
Public stream and object layouts stay the same.

Raw byte and block reads record errors and interruption across threads.
A later retry or clean EOF cannot remove an earlier failure. A used stream
closed before EOF refuses negative evidence. The scope permits at most
64 live streams and 64 active reads.

The Lua VM also counts opened streams. Every opened stream must reach EOF.
Deleting an unread stream cannot remove the incomplete result. Other read
forms and explicit filtering refuse negative evidence. An ordinary Lua VM
keeps its existing read behavior.

Seal runs after VM close and joined stream cleanup. It requires no active
stream or read and no recorded failure. It closes admission even on failure.
`NOT_APPLICABLE` also requires no lookup I/O.

## Filters and HTTP

Negative evidence admits the selected native filters `prefetch`, `cache_read`
and `inflate`. Other selected filters preserve their output but refuse this
evidence. Each failed filter constructor must leave the source offset fixed.
The check runs after resource cleanup for each candidate. A later accepted
cache cannot hide an earlier consumed prefix. A Peek-only decline is neutral.

Prefetch and cache use the common observed reads, including block sources.
Their native workers retain the scope. Inflate must reach `Z_STREAM_END`.
Truncated input, zero output before that end, or an unused encoded tail
refuses evidence. Existing decoded bytes and return values stay the same.

HTTP has a private parser completion witness. H1 permits exact fixed length
or a clean connection end. Chunked input needs a valid terminal chunk and
CRLF. H2 needs END_STREAM, no pending input, no error or interruption, and
no Transfer-Encoding header. Declared lengths and complete ranges must agree
with delivered bytes.

Any raw HTTP error or automatic resume refuses negative evidence, including
a recovered read. Live input also refuses it. The HTTP adapter retains its
existing returned blocks, retry behavior and EOF flag.

## Verification and scope

The fresh normal Core, Lua, HTTP and filter closure passes 13 native suites
with no failures or skips. The fixtures include 22 observer groups, six real
filter admission controls, 14 HTTP file groups and three chunk fault groups.
The actual weak probe must run before the real cache accepts its source.

The compiled decoder and current scripts also pass separately. The four
provider outcome fixture has 30 cases. The existing MusicBrainz first-return
fixture has 12 cases. These use controlled data and make no live request.

All 13 focused sanitizer suites pass with no sanitizer finding. They rebuild
affected Core, Lua and HTTP source. Other Core objects, Lua helpers and the
Lua VM remain normal dependencies. The focused group also covers the actual
HTTP adapter and fetcher. It is not a fully instrumented app build.

Both proofs retain exact source and library hashes. The completed sanitizer
executables and overlays have verified lossless archives. The known obsolete
`--quiet-synchro` fixture warning is separate from sanitizer diagnostics.
These checks make no native Windows or macOS execution claim.

## Remaining activation work

The Qt context can now consume a typed negative outcome into a native-only
authority. Its 31 normal and 31 focused sanitizer cases pass. The Service and
owner still need to use that authority through the complete aggregate. The
host needs a typed portable attempt
record and the exact captured provider plan. A generic guest status cannot
set a native status bit.

The native final helper now joins a sealed negative result with the parent's
prepared delta under the original artwork, status and identity tokens. It
passes 12 fresh-Core normal and eight focused sanitizer suites. The Qt host
still needs to supply the complete plan and its retained authority. An
unavailable, missing, canceled, denied, invalid or stale attempt blocks that
result. Image failure also blocks it. At least one applicable lookup must occur.
See [the aggregate contract](../native-artwork-outcome-contract.md).

Keep every working Core provider until its add-on path passes the complete
consumer, provider and app checks. This change removes no provider.
