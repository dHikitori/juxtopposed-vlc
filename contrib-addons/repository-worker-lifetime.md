# Repository worker lifetime

Repository staging and update checks use `AddonRepositoryJobs`. The manager
must have a registered app lifetime before it can start either request.
A missing handle refuses the request and clears the checking state.

Each manager permits one pending request. The process permits two active or
retiring requests. A request retains its process quota until its worker,
captures, and Qt thread storage have ended.

The worker captures the repository address, tag, settings directory, and result
storage. It does not capture the manager. The Qt owner holds the completion
callback. A locked posting gate prevents delivery after owner destruction.
The owner destructor cancels work and returns without a thread wait.

Cancellation sets a native atomic flag. `RepositoryCancellation` exposes that
flag only in the worker scope. Each Git process checks it before start, during
its bounded wait loop, and after process exit. A canceled process uses the
existing kill and wait path. Direct package tests can still use QThread
interruption; both cancellation sources apply.

The app lifetime joins Qt work before Qt teardown. A blocked file operation
can delay that join. The app shutdown coordinator keeps the event loop alive
and reports a timeout. It does not destroy Qt early or terminate the process.

The public package review flow stays the same. Busy remains true until the
owner receives a terminal result. Cancellation cannot publish a package.
Update checks leave `checking=false` after completion or submission failure.
The host still requires explicit approval before it installs a staged package.

## Validation

The repository worker passes 11 normal cases and 11 sanitizer cases. The
sanitizer run uses ASAN, UBSAN, leak checks, and stack-use-after-return checks.
It reports no diagnostics. The same worker passes 11 cases through the shared
host test build.

The package suite passes 33 cases. The runtime suite passes 16 cases and skips
one check because its worker path is absent. That exact check then passes with
the packaged worker path. Its separate run reports three passes, which include
test setup and cleanup. No test uses a real account key or a network request.

The new full Qt dispatcher case remains pending. It needs the fresh normal
Qt module.
