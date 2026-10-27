# Add-on names

Use Hakone for each name that an add-on author can use or see in the new API.

| Surface | Name |
| --- | --- |
| New package ID prefix | `org.hakone` |
| Settings schema annotations | `x-hakone-*` |
| Message SDK crate | `hakone-addon-message-v1` |
| Artwork provider SDK crate | `hakone-artwork-providers` |
| Wasm import module | `hakone` |
| Wasm entry points | `hakone_alloc`, `hakone_dealloc`, `hakone_dispatch` |
| Source reference format | `hakone-addon-source-v1` |
| Admitted source URI prefix | `hakone-addon-source:v1:` |
| Project domain | `hakone.org` |
| Add-on repository base | `https://github.com/hakone-org/` |

The repository URLs in the examples are proposed release locations. They do
not prove that a repository or tag exists. Keep an installed release pinned
to its verified commit and package digest.

Keep historical copyright and source notices. The five packages that retain
the old native provider code keep their existing `org.videolan` identities.
The internal native C and C++ names, host build targets, and host executable
names can remain VLC names until the full product rename.

The new host accepts only the Hakone source format and canonical URI prefix.
It still recognizes the old source URI marker to refuse playback authority
and unapproved artwork access. This marker does not enable the old protocol.
