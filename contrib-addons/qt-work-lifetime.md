# Qt work lifetime

The app registers one `AddonStreamLifetime` handle before it creates add-on
services. Image reads, image decode, private storage, visualizer rendering, and
visualizer parameter workers use that handle. A missing or closed handle
refuses new work.

Each joined worker retains its captures and process quota until the worker and
its Qt thread storage have ended. The process permits 40 joined Qt work items.
Each service also keeps its existing limits. Image and renderer quotas remain
held through the join, including after the owner closes.

Cancellation sets native state before it returns. Background work uses owned
values and a locked posting gate. It does not access the Qt owner after that
gate closes. Queued delivery checks the app handle and the service state.
Closing an owner suppresses late results and returns without a thread wait.

Private storage runs the admitted operation directly on its counted worker.
It does not wait for the global Qt thread pool. An unrelated blocked pool task
cannot retain a completed storage request. A native file operation that is
already running can delay the app drain. The shutdown coordinator keeps Qt
alive until that work ends.

The visualizer parameter worker has its own event loop. A short native-state
check stops its process and ends that loop after cancellation. The worker
quota remains held until Qt thread storage has ended.

The app retirement hooks close image and visualizer services, the native
library store, and the library target picker before the drain. The shutdown
coordinator closes the app handle and retains the main event loop. Tests also
close and join their app handle before they destroy the Qt application.

## Validation

The current normal checks pass 153 cases across 11 affected suites: images 29,
storage 21, visualizers 13, visualizer resources 12, render queue 12, parameter
runtime six, native player analysis six, stream shutdown 20, visualizer
surface three, metadata service 19, and app shutdown 12.

The storage path also passes 21 focused address and undefined-behavior
sanitizer cases, with leak and stack checks enabled. Diagnostics are empty.
Those checks instrument storage, host dispatch, the shared lifetime, and the
test. They do not instrument every remaining host dependency.

The full Qt module build and compiled UI run remain separate completion gates.
The checks above do not prove those gates or the remaining supported platforms.
