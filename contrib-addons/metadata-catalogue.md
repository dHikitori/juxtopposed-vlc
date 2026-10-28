# Complete metadata provider snapshots

The host captures the complete installed package list before it selects metadata
providers. This list is private host data. It is not an add-on API.

`PackageStore::metadataCatalogueReader()` captures an owned store and its current
authority token. This step reads no file and creates no directory. The reader
runs in joined app work. It holds one `store.lock` while it reads current provider
preferences, distribution policy, safe-mode state, and each installed package.
It retains both the token captured before the work was queued and the token
captured under the lock. Both must stay current.

The snapshot keeps each package and each declared contribution before any
feature, type, media-kind, enable, or configuration filter. Disabled packages,
packages that need setup, and packages with no metadata contribution remain in
the snapshot. This prevents an incomplete list from becoming evidence that all
providers returned no result. The caller derives configuration and account scope
from the captured schema and settings plus its owned credential reader. A
missing credential-cache entry does not prove that a saved key is absent.

A bad current package, policy, schema, resource, or identity refuses the whole
snapshot. The reader does not substitute a backup or return a valid empty list
after a read error. It checks the full package digest and the same settings and
resource rules as the directory loader. `Manifest::validateContents()` supplies
that shared check without another file read.

The limits are 256 package candidates, 256 raw contribution declarations, and
1 MiB of retained canonical descriptor data. These limits apply before provider
selection. The byte limit does not limit total verification I/O: each complete
package still uses the existing 16 MiB file and 64 MiB package limits. The file
reader refuses links, nonregular files, and oversized files before it reads
them. On Linux it also refuses a file changed to a FIFO or link between the
initial file check and open.

The authority tokens cover host store mutations. They do not create an operating
system lease against unrelated external file or keychain changes. A later
provider attempt must still check its exact release, grants, settings, account,
request, and native admission. This snapshot alone cannot set an artwork
`NOTFOUND` status or activate the native metadata owner.

## Verification

The exact production sources pass 34 catalogue and 13 existing store-authority
cases on WSL Linux. The same cases pass with AddressSanitizer,
UndefinedBehaviorSanitizer, float-conversion checks, and leak detection. There
are no failed or skipped cases and no sanitizer diagnostics.

The cases cover the raw package and contribution limits, retained byte limit,
one exact store lock, changes away and back, corrupt nonpreferred packages,
invalid settings with a matching release digest, and file-type swaps before
open. An actual joined worker keeps the reader alive after its original store
owner is destroyed. The reader factory creates no directory on the caller.

The focused proof compiles eight current production files. It uses no previous
host archive or aggregate metadata file. The input hashes, commands, logs, and
results are in `/tmp/hakone-metadata-catalogue`. The reviewed patch hash is
`b296c755c0dae4212ade3647fa34595b9cd0957abb2ed284ed66a1ee4b57f691`.
The Windows file-reader branch still needs its platform build and runtime check.

Automake, Meson, and the standalone CMake build register the test. Linux test
targets enable the file-open interception checks; other platforms do not claim
that coverage.
