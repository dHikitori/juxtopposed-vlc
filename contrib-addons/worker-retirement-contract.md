# Worker retirement and callbacks

A worker failure closes admission at once. The failed object cannot start
another process. Manager can create a fresh worker after withdrawal. Normal
Stop can still end a healthy worker and permit a later fresh process namespace.

Terminal callbacks leave the QProcess signal stack before they notify an
observer. An observer can delete the worker or Manager. Read, cancel, and stop
paths check the retained owner after callbacks. Queued unavailability retains
a weak worker reference and owned package/error values. A late event cannot
withdraw a replacement worker.

Withdrawal uses an owner-thread package gate. It detaches the old request map,
invalidates native authority before Worker stop, then cancels native work and
delivers only the detached terminal set. New same-package admissions are
refused during this cleanup. Other packages remain available. The final
`packageWithdrawn` signal runs after the gate is removed, so an observer can
start fresh work. An outer retry reuses that exact replacement instead of
overwriting it. Package approval and removal respect a refused withdrawal.

Host service cancellation can also delete its dispatcher. Each package loop
checks the owner after a native cancel or completion callback.

The coherent normal group passes 240 cases: runtime 31, broker 70, host
services 13, session host 32, WSS 15, native authority ten, and packages 69.
Focused sanitizers pass all 31 runtime and 13 host service cases with no
diagnostics. The tests include actual process startup failure, deletion from
completion and withdrawal observers, and a real temporary worker that records
both fresh invocation cancellations after reentry.

The sanitizer build instruments Manager, Worker, HostServices, CredentialStore,
and the affected tests. It uses a pinned matching normal host archive for
other dependencies. The real app session mount and full app checks remain open.
