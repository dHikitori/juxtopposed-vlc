# Native request guards

The C++ Manager and Worker APIs accept an optional request guard as their fourth
`invoke` argument. The existing three-argument methods stay available. No guard
enters a guest message. The guard must retain plain native or memory state. It
must not read files or use a QObject.

Each invocation combines this guard with its captured package configuration.
It also has a shared active flag. Cancel, completion, or a failed check closes
that flag. A retained host context cannot become current after the invocation
ends. The check treats an exception as refusal.

Worker checks the invocation before guest dispatch, each host call, each host
reply, and terminal delivery. A failed check cancels only that invocation and
its broker and host-service jobs. Known late packets are ignored. Other
invocations in the same worker can continue.

The combined guard also goes to the HTTP broker and host-service context.
The broker checks before name lookup, after address lookup, after credential
access, before socket admission, during response reads, and before delivery.
Host adapters must check the context again at their own asynchronous admission
points. A check does not replace the native lease or cancellation barrier for
an effect that has already entered Core.

The isolated recipe is `modules/gui/qt/tests/invocation-guard/CMakeLists.txt`.
It compiles the changed Manager, Worker, and broker sources. It uses a pinned
coherent host archive for unchanged dependencies and Qt metaobjects. The new
tests use a real child process. They queue guest packets before native Stop
and process the packets before the Qt cleanup event. They also check sibling
requests, late completion, handler guards, exceptions, owner deletion, and
broker admission. The existing broker suite is a separate regression target.

The focused suite passes 14 normal and 14 sanitizer cases. The broker suite
passes 70 normal and 70 sanitizer cases. The existing runtime suite passes
31 normal and 31 sanitizer cases, including its packaged-worker path and
callback deletion and replacement cases. The group passes 115 cases in each
build. There are no failures or skipped
cases. Both sanitizer diagnostic files are empty. The checks enable address,
undefined-behavior, floating-cast, leak, and stack-use-after-return detection.
Only the changed sources and test sources use sanitizer instrumentation. The
pinned unchanged host archive and Qt libraries do not use instrumentation.

The actual Manager case installs the local test package and uses a temporary
test credential. It runs a child fixture instead of the production guest. The
broker admission cases stop before socket use. No public network, Windows,
or full app execution is claimed by this focused proof. The normal app build
must compile these changed sources before the production source owner uses
the fourth argument.

The exact source, archive, binary, and log hashes are in
`/tmp/hakone-invocation-guard-validation.json`. The tests retain both original
three-argument symbols and both guarded overloads. The Manager gains no data
fields or Qt metaobject entries. The normal CMake tests register the Unix
process fixture, and Automake distributes its recipe and source. This group
adds no MainUI mount or guest packet field.
