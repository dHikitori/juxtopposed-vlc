# Host provider names

Installable packages cannot use `hakone.builtin` or `vlc.builtin` as their
package ID. They also cannot use a name below either dotted prefix. The host
owns these names. The old prefix stays reserved during the Hakone name change.

A package ID and a contribution ID form the global provider ID. Without this
reservation, a portable package could use the same ID as a native host
provider. Its installed row could then carry different enablement or cache
provenance from the native callback selected for that ID.

Manifest parsing refuses the reserved names before package inspection or
installation. The store also parses manifests when it checks candidates and
reads installed snapshots. This rule applies to every contribution type.
Neighbor names such as `hakone.builtin-tools` remain available. Project add-ons
under `org.hakone` and extracted add-ons under `org.videolan` remain available.

The complete package suite passes 76 normal and 76 focused sanitizer cases.
Six new rows check both reserved roots, direct provider names, and nested
provider names through the parser and directory import. One further case
checks allowed neighbor names and the two project prefixes. The 69 earlier
package cases still pass. There are no failures, skipped cases, or sanitizer
diagnostics.

The focused recipe compiles the actual manifest source and package tests. A
coherent normal host archive supplies the other components. Only the parser
and test sources use memory instrumentation. Source and artifact hashes, build
records, and results are in `/tmp/hakone-package-namespaces-validation.json`.
The [builtin name migration](records/builtin-provider-id-migration.md) records the
Hakone IDs and saved-preference handling. The fresh full app build remains
open.
