# Native metadata provider owner

The factory prepares one native provider owner for one metadata service. It
reserves the service before it installs providers. It activates the Core
owner only after the provider context, image reader, materializer and family
plan are ready. Activation is off by default. The main interface does not yet
activate this factory.

The owner supplies four named artwork families: MusicBrainz, Google Images,
French TV and Last.fm. The family plan selects one implementation for each
family before execution. An automatic request with a native anchor can select
the named native implementation. A manual or portable request retains the
portable path. A failed portable attempt cannot cause a hidden retry through
the same native family.

Each provider binding has its own validation, image resolution and cancellation
functions. A native claim must match the exact pending job and family. The
private anchor scope does not enter a guest request. The native image result
retains its approval through final Apply. Source credit uses the ordinary
`name` and `url` fields.

## Lifetime

Create, use and destroy the owner on the service thread. Keep the native parent
valid until app work has drained. The owner does not retain that raw parent
after setup.

The service permits one reserved owner. Each registration has an exact owner
and a binding generation. An older owner cannot remove a new binding.
Shutdown closes the native context and image work. It also cancels native
jobs whose provider callback has already ended. Native cancellation does not
need a Qt event.

Cleanup can call external code. That code can delete the owner and create
another owner at the same address. Cleanup retains a `QPointer` identity and
checks it after each such call. It completes detached native cancellation
before it returns. It cannot clear the new owner's plan, providers or image
adapter.

## Verification

The five-suite baseline passed 104 normal and 104 focused sanitizer cases.
It covered the coordinator, family plan, service, native context and owner.
After the cleanup repair, the owner suite passed 11 cases in each build. Both
runs had no failures, skips or sanitizer diagnostics. The exact earlier owner
body failed only the new address-reuse case.

The Linux test uses linker wrappers to reserve one allocation for two real
owner lifetimes. Other allocations use the matching C++ allocator. The test
supports the 32-bit and 64-bit allocation symbol names. All three build
inventories register the same test and wrapper options. Execution was on
64-bit Linux.

Affected sources and generated meta-object code were fresh for these checks.
Other host archives, Qt and the shared Core used normal builds. Provider
responses and image data used controlled fixtures. These checks do not prove
a full app link or live provider parity.

## Activation gates

Keep the existing Core provider paths until the owned route passes the full
consumer and provider checks. No provider is removed by this change.

Positive fetched status already uses a guarded native transaction. Exhaustive
remote not-found status still needs sealed attempt outcomes, complete stream
evidence and a ranked aggregate. An unavailable or incomplete lookup cannot
set not-found status. See [the outcome contract](native-artwork-outcome-contract.md).

Identity selection, accepted identity persistence, user-cleared field policy,
cache retention and the full app checks remain separate work.
