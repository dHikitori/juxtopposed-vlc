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

Before owner activation, finish the recorded callback retirement follow-up:
cancel a returned invocation through a lifetime-safe callback if its owner
was deleted during invoke, and check the owner after detach cancellation.
