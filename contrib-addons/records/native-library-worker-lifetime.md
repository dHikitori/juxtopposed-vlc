# Native library worker lifetime

The `addon-native-library` queue uses `MLJoinedThreadRunner`. Other library
queues use the existing `ThreadRunner`.

The app must register its `AddonStreamLifetime` handle before it creates
`MediaLib`. A missing or closed handle refuses a new add-on library task.
The runner permits 16 tasks per owner and 16 active or retiring tasks per
process. The owner limit includes completed results that await Qt delivery.

Each task has its own joined Qt worker. A private native queue orders the work
callbacks. Cancellation sets a native atomic flag and wakes that queue. It
does not require a Qt event. A canceled queued callback does not enter the
library. A running callback can finish its admitted native transaction.
Cancellation can then suppress delivery. It does not reverse that transaction.

The existing native work lease remains in place. The caller acquires the lease
before submission. The worker calls Begin before native library work and End
after that work. Its work closure releases the lease on the counted worker.
The app lifetime then joins the worker and its Qt thread storage. It retains
the process quota through that join.

Only the Qt owner stores result callbacks and receiver connections. A worker
posts an opaque task ID through a locked owner gate. Owner destruction closes
the gate and cancels tasks without a wait. No worker calls a receiver or reads
a `QPointer`.
Keep each receiver on the owner's Qt thread until delivery or cancellation.

On app retirement, close the native library store and target picker before the
drain. Their close operations invalidate the native admission state. Close the
app handle and keep its event loop alive until joined Qt work has ended. The
MainCtx retirement hooks perform these operations in the host mount.

Both runners use one process task-ID allocator. IDs never repeat. Zero means
that submission did not occur. The allocator does not use the last numeric
value.

The focused tests use actual `QThreadStorage` destruction. The full Qt fixture
also tests the mounted `MediaLib` dispatcher with a real Core library instance.
That fixture requires a fresh normal Qt module. A native lease alone does not
prove that Qt thread storage has ended.

## Validation

The joined runner passes 17 normal cases and 17 sanitizer cases. The sanitizer
run uses ASAN, UBSAN, leak checks, and stack-use-after-return checks. It reports
no diagnostics. The existing `ThreadRunner` suite passes 40 normal cases.
The two new cancellation cases release the last callback capture and delete
the runner during cancellation or receiver removal.

The new full Qt dispatcher case remains pending. It needs the fresh normal
Qt module. These focused results do not prove that app integration.
