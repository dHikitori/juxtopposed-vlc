# Built-in provider IDs

Host provider descriptors use the `hakone.builtin.*` package namespace. The
subtitle adapter uses `hakone.builtin.opensubtitles/search`. Native C API
names and internal hash domains do not change.

Manager accepts only Hakone IDs for a live built-in registration. The
descriptor package must match its contribution ID. Manifest reserves both
the new and old namespaces against add-on packages.

On load, Manager converts old `vlc.builtin.*` provider order and enablement
preferences in memory. This step does not write the settings file. An
explicit Hakone enablement value wins over its old alias. An explicit Hakone
order entry also wins its position. Repeated aliases become one entry.
Other provider IDs keep their names and relative order.

The next explicit order or enablement change saves the converted document.
The migration does not register a provider, enable an add-on, or create a
permission grant. Old registration IDs are refused.

The isolated recipe is `modules/gui/qt/tests/builtin-ids/CMakeLists.txt`.
It builds the current Manager, Worker, broker, and Manifest with the retained
host archive. It also compiles the actual subtitle service with the strict
Qt flags. The subtitle object check does not link the old native metadata
archive to the changed Coordinator layout. The full subtitle suite needs
the next coherent native metadata build.

The runtime suite passes 35 normal and 35 focused sanitizer cases. The
installer QML suite passes 29 cases. There are no failures, skipped cases,
or sanitizer diagnostics. The actual subtitle service object also compiles
with the strict Qt definitions. Its 53-case execution suite remains pending
until the metadata service and Coordinator have a coherent build.

The exact source, binary, object, and log hashes are in
`/tmp/hakone-builtin-ids-validation.json`. The normal runtime suite also runs
the packaged portable worker with no ambient credential. The focused
sanitizer build instruments Manager, Worker, the broker, Manifest, and the
runtime test. The retained host archive and Qt libraries have no sanitizer
instrumentation. This checkpoint does not claim a full app or Windows run.
