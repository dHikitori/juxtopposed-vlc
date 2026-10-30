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

## App and source names

Public add-on schemas and attributes use `hakone`. Proposed example repository
addresses use `github.com/hakone-org/`. New packages use `org.hakone` IDs.
Packages extracted from Core retain their existing package IDs.

Add-on messages use "the app". The credential store's display label uses
"Hakone add-on credential". Internal credential lookup identities retain
their current values.

A distribution source identity can use a canonical HTTPS address or
`hakone-distribution:<identity>`. Only the distribution source kind accepts
that prefix. The prefix form refuses an empty suffix, path, query or fragment. The older
`vlc-distribution:` prefix is refused. Source approval still binds the exact
installed contribution, account and media profile.

The source identity suite passes 36 normal and 36 focused sanitizer cases.
There are no failures, skipped cases or sanitizer diagnostics. The naming
change covers nine valid, malformed and wrong-source-kind rows.

## Preference migration

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
