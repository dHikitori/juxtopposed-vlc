# Reserved provider names

Installable packages cannot use `hakone.builtin` or `vlc.builtin` as their
package ID. They also cannot use a name below either dotted prefix. The host
owns these names.

A package ID and a contribution ID form the global provider ID. Without this
reservation, a portable package could use the same ID as a native host
provider. Its installed row could then carry different enablement or cache
provenance from the native callback selected for that ID.

Manifest parsing refuses the reserved names before package inspection or
installation. The store also parses manifests when it checks candidates and
reads installed snapshots. This rule applies to every contribution type.
Neighbor names such as `hakone.builtin-tools` remain available. Project add-ons
under `org.hakone` and extracted add-ons under `org.videolan` remain available.
