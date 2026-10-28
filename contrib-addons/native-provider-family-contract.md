# Native provider family boundary

This boundary retains the app-shipped native artwork providers. It does not
activate a new metadata owner or remove a provider. The host must choose one
implementation for each provider family before execution. It must not retry
the same family through Lua after a portable provider returns an error or no
match. The final family selector and native image adapter remain open work.

## Exact native request

The private Core API is in `src/preparser/metadata_native_provider.h`.
`Probe` accepts an existing native metadata job and one named artwork family.
It cannot construct a job from a URL or a public media reference. The job must
be pending and must request network access and an artwork field. Its snapshot
must be ordinary native media. A policy-scoped item or a raw source URI is
refused before provider execution.

The snapshot holds the exact job, item, policy, media URI, metadata tokens,
URI/name/options tokens, and request generation pair. The named fetcher probe
runs one app-shipped family. It does not download or publish artwork. A
successful result can issue one opaque claim with a fixed artwork URI of at
most 8,192 bytes. This native image route accepts HTTP or HTTPS. It retains the
native provider behavior; it grants no add-on network permission.

`MatchesJob` compares the exact job and snapshot with those saved values. A
same-value metadata write, URI change and restoration, name write, policy
change, job cancellation, or provider close refuses the claim. A claim grants
no write authority. Final native Apply must still validate the live target.
Only the future trusted native image adapter can consume the claim. A guest
URL map and the original embedded-artwork permit cannot create this claim.

## Lifetime

The app must admit a counted join before `New` creates native executors. The
private child object retains the native read context. Its parent must remain
valid until `Delete` returns. `Close` withdraws authority and cancels native
jobs without a Qt event. `Delete` runs on counted cleanup work, closes the
fetcher, and waits for native API calls, callbacks, and held request cleanup.
Do not call `Delete` from a callback or from a call that it must drain.

At most 32 probe requests can remain retained. Each request enters the native
list before fetcher submission. Close cancels those exact jobs outside the
provider lock. A later submission then observes cancellation. Cancel is safe
for an unknown job and does not change that job. A retained closed provider
control has no parent or executor after Delete. A retained claim remains
invalid after Close.

A claim retains its native job and backend. The counted app cleanup must
release every claim before Core and Qt teardown. A returned cancel control
must not retain a claim after that join.

The Qt scope is separate native host data. `enrichNative` passes it through a
private C++ hook. It does not enter portable provider context. Native claims
cannot share a request across different anchors or reuse a cached result.
Portable providers retain their usual request sharing. The service accessor
returns a held original Core job only for an active native invocation.

## Focused evidence

The isolated fixture compiles the real provider wrapper and uses real Core
items, metadata jobs, and revision tokens. It controls only the native fetcher
execution boundary. Six groups pass normally and with AddressSanitizer,
UndefinedBehaviorSanitizer, and leak detection. They cover 18 native contexts,
exact job/family claims, snapshot tokens, provider errors, result URI bounds,
scoped and offline refusal, unknown cancel, the 32-request limit, inline Close reentry, submission after Close,
and a blocked cancel callback through joined cleanup.

The fixture disables plugin scan and cache because it executes no plugin.
An earlier run with full module scan reports 20,918 leaked bytes in 145
allocations from unloaded module frames. A separate libVLC startup/shutdown
control with no provider wrapper reproduces that exact report. The focused
fixture uses no leak suppression. Build recipes, logs, and the control source
are in `/tmp/hakone-native-family/`.

The normal Core build also compiles the registered wrapper and fixture. Its
six groups pass against the current shared libraries. All 12 private entry
points are present in the Core export table. This check still uses the
controlled fetcher boundary.

This evidence does not prove a live provider, the native image route, the
final family selector, or full app integration.

The separate native scope checkpoint now passes a fresh host/native build:
coordinator 28, service 29, and artwork regression 34 normal cases. Focused
sanitizers pass all 28 coordinator and 29 service cases with no diagnostics.
The four new cases prove private scope isolation, separate native anchors,
ordinary portable request sharing, and exact active native job access.

The callback retirement follow-up now passes all 30 coordinator and 34 service
cases normally and with focused sanitizers. An invocation retains its copied
invoke and cancel callbacks through return. If invoke deletes the owner, the
returned request is canceled. Each cancellation boundary checks the owner
before it accesses service state again.

Consumer state is removed before native cancellation. Detached artwork jobs
are canceled through a copied callback even if the native job or the first
asset cancellation deletes the service. Two separate multi-asset cases prove
that every detached job ends exactly once. The prior coordinator implementation
fails the returned-request cancellation control. Exact hashes and test records
are in `/tmp/hakone-native-callback-retirement/final-proof.json`.

The app Service/factory mount and owner activation remain open.

## Qt adapter boundary

The new adapter sources are not active in the app. Focused runtime tests now
pass. The following rules define this adapter boundary.

`AddonNativeProviderContext` admits the app join before it creates the native
context. Native callbacks store plain results in a bounded mailbox. Completion
captures remain in the Qt owner map. Close retires those captures on the owner
thread and cancels native work. The counted cleanup drains the native context
and releases its claims and jobs before native parent teardown. A retained
cancel function contains only a weak native state reference and a request ID.

The coordinator receives a random claim reference. It receives no native
pointer or artwork URL. Validation requires the exact job, snapshot, and
selected family. The native image adapter consumes that reference once. It
uses the existing bounded native reader, decoder, raster cache, consent epoch,
and counted join. The original embedded-artwork entry cannot accept a provider
claim. A guest descriptor cannot create native authority.

The result retains its native consent permit. A direct manual caller must
admit the final callback-free native Apply through that permit. A completed
download does not bypass later consent withdrawal. Automatic pipeline work
must also retain its native parent anchor through final publication. The host
must close both GUI owners before it waits for app drain.

The four retained Lua families do not supply strong entity evidence. The
native compatibility rule can fill only missing artwork. It cannot supply a
title, an episode field, or a replacement image. It records the native family,
`native-family-artwork@1`, the provider cache partition, and available source
credit. It does not copy request identity into provider evidence. Portable
providers retain the strict identity rules.

The host family table selects one eligible native or portable implementation
for each family before execution. Saved order and enablement apply. Native
defaults retain the original family order. A portable miss or error cannot
start the native implementation of that same family. A higher strong match
keeps priority; an unresolved strong ambiguity blocks automatic native art.

The controlled Qt fixture passes all 15 cases normally and with focused
AddressSanitizer, UndefinedBehaviorSanitizer, and leak detection. It covers
opaque reference scope, native cancel, GUI capture release, blocked join,
owner deletion, the 32-request limit, and retained cancel cleanup. It also
proves exact image pixels, the sealed image route through real native Apply,
refusal of existing artwork, and refusal after consent withdrawal. A delivery
retains its local native references through callback return. A last completion
capture can delete the GUI owner with another result ready.

The coordinator passes 34 cases, and the family table passes seven cases,
normally and with the same focused sanitizers. No sanitizer diagnostic or test
skip occurs. The Core bridge, changed production sources, and fixture sources
use the sanitizers; Qt, the shared Core, and unchanged image/lifetime dependency
objects use their normal builds. These cases do not prove a live remote
service, the Service/factory mount, or final app integration. Exact source
hashes, commands, and logs remain in `/tmp/hakone-native-materializer/`.

## Sealed Qt outcome bridge

The Qt context now has a separate host-only `probeOutcome` route. A closed
negative receipt cannot enter the image adapter. Image validation and claim
consumption require `MATCH` and a nonempty native artwork address.

`takeOutcome` consumes one exact pending child, family and negative receipt.
It converts `NO_MATCH` or `NOT_APPLICABLE` into a native outcome authority.
That authority retains the provider reservation and admission gate, with no
child, backend, claim or Qt capture. The complete aggregate must retain it
through final native Apply. Wrong bindings, stale facts, failure outcomes and
duplicate consumption refuse the result and revoke admission.

The counted native call spans the complete conversion and the consumed
claim's destruction. Repeated retirement reaches native Close even while an
earlier cancellation callback is blocked. The app join waits for every native
call and retirement stack before it releases the context and parent.

The current context passes 31 normal and 31 focused sanitizer cases. Two
controlled pauses verify conversion during retirement and repeated Close
during a blocked native callback. The tests also release all caller job and
backend references while they retain an outcome authority. They verify that
the backend can then end. Positive image materialization still passes through
both probe routes, including consent withdrawal.

These tests compile current context, image, lifetime and native provider bridge
sources together. They control the finder result. Shared Core, LibVLC and Qt
remain normal dependencies. They do not prove a live remote lookup or the
complete ranked Qt aggregate. App owner activation remains off.
