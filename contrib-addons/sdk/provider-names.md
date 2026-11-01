# Reserved provider names

Installable packages cannot use `hakone.builtin` or `vlc.builtin` as their
package ID. They also cannot use a name below either dotted prefix. The host
owns these names.

A package ID and a contribution ID form the provider ID, so the reservation
stops a package from taking the ID of a provider built into the app. The
validator and the install refuse these names for every contribution type.
Neighbor names such as `hakone.builtin-tools` are available.
