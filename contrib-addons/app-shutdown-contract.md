# App shutdown and add-on work

The Qt interface owns one `AddonAppShutdown` after it constructs `QApplication`.
It registers that exact lifetime handle for add-on services.
It removes the registration only after work joins and interface cleanup ends.
A missing registration must refuse new asynchronous add-on work.

## Host service hooks

`MainCtx::addonStreamLifetime()` returns the retained app handle.
Call this getter on the app thread. The returned handle is thread-safe.
QML and guest operations cannot replace this C++ handle.

Connect native and Qt posting-gate closure to `MainCtx::addonWorkRetiring()`.
Use a direct connection on the app thread. The close action must not wait for GUI events.
This hook runs after admission closes and before the coordinator waits for work.

Connect resolver deletion and consent closure to `MainCtx::addonWorkJoined()`.
This hook runs after registered Qt work and its thread cleanup join.
The coordinator then processes deferred deletion before it releases Core teardown.
Do not close consent on the GUI thread while a saved-file check can hold its mutex.

The retiring and joined hooks each run once.
Retiring-hook reentry cannot start the joined phase before that hook returns.

MainUI binds its metadata service to the exact app handle before it exposes the
service to consumers. A refused binding closes the service. Both app retirement
and Manager retirement close its native posting gate before worker shutdown.
The native queue owner also removes its playlist listener at these early hooks.
MainUI destruction closes both owners before automatic child destruction.

## Quit paths

The dialog provider first requests add-on retirement.
It calls `libvlc_Quit` only after the joined barrier.
A refused bounded request resets its dying state and uses one retry timer.

External Core cleanup closes native source and metadata owners before interface cleanup.
The Qt interface then queues its retirement request on the app thread.
Core waits on its teardown thread while the GUI event loop remains active.
The native Close completion has a reserved slot outside the 16 general callbacks.
Interface cleanup starts only after the joined phase.

The final Core reference calls `triggerQuit`.
This closes the remaining windows and requests the authorized app exit after retirement.
The existing Core caller contract requires interface teardown outside the Qt thread.
The macOS launcher dispatches `libvlc_release` on its interface queue.
The Unix launcher also releases Core outside the Qt interface thread.

An OS Quit event uses the same retirement path.
A direct `QCoreApplication::exit` cannot bypass the final guard.
If `exec` returns too soon, the interface resumes its event loop.
It requests normal Core shutdown and waits for an authorized drained exit.

A compositor startup failure also keeps an event loop until work joins.
It then reports interface failure and destroys its Qt settings.
Failures before app construction have no registered Qt work.
The debug-option allocation failure wakes the native interface-ready waiter.

## Timeout and coverage

A timeout reports that work did not stop. The coordinator retries while Qt remains alive.
It does not terminate the process. It never waits indefinitely on the GUI thread.
Every app destruction path must pass the final joined guard.
An unexpected prior registration closes admission and waits for its Qt work before startup fails.
The interface does not replace or remove that other registration.

The standalone Unix launcher has an existing signal-shutdown watchdog.
External SIGTERM or SIGINT can force whole-process termination after its deadline.
That path bypasses Qt object teardown. It does not permit an early Qt destructor.
Normal UI quit joins work before `libvlc_Quit` starts that shutdown path.
This group does not change launcher process policy.

This barrier covers work registered with its app handle.
Detached image, render, visualizer, repository, and native-library Qt work must use a real join boundary.
A publication epoch or a canceled callback does not prove a thread join.
Core media-library lifetime leases protect Core modules. They do not replace this Qt barrier.
The existing startup RHI probe has a separate lifetime contract.

Add-on Play remains disabled until its backend and normal app checks pass.

## Checks

The isolated recipe is `modules/gui/qt/tests/app-shutdown/CMakeLists.txt`.
The tests run actual child `QCoreApplication` event loops.
They cover startup failure, direct exit recovery, and a native teardown-thread request.
They also cover timeouts, hook order, reentry, bounded requests, registry closure, and deferred deletion.
The normal Qt module build must verify the `qt.cpp`, MainCtx, and dialog-provider mount.
