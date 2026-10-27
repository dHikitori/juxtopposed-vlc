# Native metadata pipeline

The native commit helper, artwork approval transfer, private pipeline, and fetcher stages are in the normal Core build. Their focused native tests pass. The preparser request mounts and Qt owner activation remain open.

## Ownership transition

A preparser can exist before the Qt metadata service. A request can also enter a native queue before that service starts.

The private legacy lease has two admission steps:

1. Acquire the lease before task submission.
2. Begin the lease on the native worker before direct legacy network work.

The metadata owner lock protects Begin and owner installation. After owner installation, a pending legacy lease cannot begin. The host cancels each running lease through its native interrupt context.

The owned backend stays suspended until every running legacy lease ends. It can accept bounded pending jobs during this interval. Cancel and Close finish those jobs without a Qt event. The last legacy End resumes dispatch outside the owner lock.

Each instance holds its own suspension. Two instances that share one backend must release both suspensions before dispatch resumes.

Close permanently retires the instance's remote metadata profile. Forget detaches stale leases from the instance. A pending lease can then outlive instance destruction without access to that instance.

## Native policy stages

The owned pipeline has separate native stages:

| Stage | Authority and result |
| --- | --- |
| Local readers, tags, sidecars, attachments, and cache | Retain the native local behavior. Preserve current user fields. |
| Ordinary native embedded artwork | Apply the user's network preference. Preserve the native HTTP and LAN scope. Produce a native asset result. |
| Add-on metadata and image claims | Use the installed contribution's account, release, settings, and approved network scope. Validate image bytes before local cache publication. |
| Retained native metadata providers | Use a named trusted adapter through the shared coordinator. Retain the provider family until replacement parity passes. |

A restricted source item cannot acquire ordinary native embedded-artwork authority. Its raw artwork field remains a missing display asset until a trusted host creates a current approval record.

An owned Qt consumer must not load an ordinary remote image before the native asset stage applies the network preference. The unowned native profile retains its existing behavior.

## Preparser mounts

The internal preparser needs a held, cancel-visible remote stage through its terminal callback. The external preparser needs the same stage in its parent process.

Child request options and child process types must omit fetch work. The parent must retain local fetch behavior after deserialization. Fetch-only requests must bypass the child pool.

Cancel must stop native publication before queued Qt cleanup. Delete must close the native request list before executor or process joins. A result callback must not wait for the Qt event loop.

The private pipeline retains a native parent guard from request submission through the final commit. The guard can record cancellation before the pipeline receives the request. Final commit and guard cancellation use the same native admission lock. If cancellation wins, no target field changes. If commit wins, cancellation returns after that commit. No callback runs under this lock.

The pipeline captures the original target before the first local stage. Each stage uses a private result copy. Each child job enters the parent's list before it can start. Cancellation can thus find a child between list insertion and dispatch.

With network access disabled, a default local-and-network request still runs its local stage. The pipeline removes network work from that request. A later consent grant cannot add it again.

The fixture has 13 test groups. The normal, AddressSanitizer, and UndefinedBehaviorSanitizer runs pass. The diagnostic run also enables leak detection and stack-use-after-return detection. The fixture covers cancellation before handoff, cancellation after result completion but before final commit, idle child cancellation, same-value user edits, policy revocation, callback reentry, and retained-token limits. This evidence does not prove the preparser mounts.

## Native publication

Every remote request uses an immutable native snapshot. Guest context receives the approved metadata projection. Native adapters can use private media references.

The commit helper compares the original target, URI, exact policy, identity tokens, extra-tag token, and proposed field tokens before any write. Same-value and ABA writes change the tokens.

Artwork publication also needs native proof from the result copy. The helper allocates that proof before admission. It publishes the proof with the reserved artwork token under the target lock. Metadata text cannot create this proof.

The player must use this native commit path for automatic results. Its old value-only copy loop remains a cutover gate.

## Remaining evidence

- Mount the transaction before internal and external Parse starts. Preserve the complete native parse result through the same guarded publication.
- Prove internal and external cancellation through remote handoff.
- Retain each native provider family through the shared owner.
- Prove local, embedded, HTTP, LAN, and disabled-network cases.
- Prove owned player and library publication without a second remote path.
- Preserve native BMP, ICO, PNM, TIFF, XBM, and XPM artwork through a bounded native decoder adapter. The current native image helper does not cover these formats.

The fetcher fixture has 13 groups. Normal and sanitizer runs pass, including the real native executor transition, local-only work, named provider probes, late cancellation, and retained context shutdown. The normal build also runs the existing native artwork fetcher regression with the actual private pipeline and owner helpers. These results do not prove full Parse publication or request mounts.
