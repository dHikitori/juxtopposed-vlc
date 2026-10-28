# Provider preference transactions

Provider rank and builtin enablement use the same add-on store authority as
package, account, grant, and settings changes. These are host C++ operations.
They do not add a guest method or a settings schema field.

`PackageStore::providerPreferences` reads a fresh saved document under the
exact profile `store.lock`. It returns normalized values and a native store
authority token. A missing file means empty preferences. A busy, malformed,
oversized, symbolic-link, or nonregular file refuses the snapshot. The caller
must not use an empty fallback as proof of an exhausted provider plan.

`saveProviderOrder` and `saveBuiltinProviderEnabled` read the current document
under that same lock, merge one change, and save the complete document. They
do not overwrite other saved changes from a second Manager. They retain
`StoreAuthorityMutation` through publication. Native watches finish before
publication. A failed write can invalidate an older lookup; it cannot restore
that lookup's old authority.

The read normalizes old builtin aliases in memory. Explicit Hakone values
keep priority. Only an explicit successful save publishes the normalized
form. The existing generated settings and provider controls use these save
methods through Manager.

The document has a 256 KiB limit and at most 256 top-level entries. Each rank
list has at most 128 distinct, nonempty IDs. An ID has at most 256 characters.
Feature names have at most 128 characters and cannot start with an underscore.
The builtin enablement map has at most 256 boolean entries. Raw lists are
bounded before alias normalization, so duplicate aliases cannot hide overflow.

A native plan must capture on joined background work. It must retain its
pre-queue authority token as well as the fresh locked snapshot token. Check
both after each asynchronous step. Retain synchronous native watches through
final commit. A fresh snapshot alone must not silently adopt a rank or account
change that occurred while earlier work was queued.

This change does not by itself supply the complete artwork attempt plan,
per-provider watches, or final negative artwork commit. It provides the
shared preference boundary that those paths need. External file edits that
do not use the store are outside the same-process watch guarantee.

The live implementation passes 90 package and 40 runtime cases normally
and with focused sanitizers. The runtime suite uses the real portable worker
for its missing-key case. The checks include stale Manager saves, synchronous
watch completion before file publication, changed-away-and-back rank state,
exact lock refusal, malformed and oversized documents, and FIFO refusal.
The checks compile current Store, Manager, StoreAuthority, RepositoryJobs,
and test code. The focused build instruments those sources. It reuses
unchanged host MOC code and other host dependencies. Qt is not instrumented.
