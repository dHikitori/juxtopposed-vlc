# Native preparser request boundary

The private request pipeline now has one terminal decision for native Apply,
cancellation, status, and sideband authority. A successful Apply records its
status and takes its prepared event arrays before it releases the admission
lock. Later cancellation cannot change that decision or remove its events.
All user callbacks run after the lock is released.

The guard retains the job through unlock. A final backend destroy hook cannot
run under that lock. Successful admission can retain sidebands when no standard
field needs a write. A stale or failed Apply grants no sideband authority.

`PushRequest` retains a local transaction when the metadata owner is absent,
closed, or suspended. Only an explicit never-owned `ENOENT` decision permits
the separate legacy remote lease. It cannot restore remote work after an
ownership transition. The private full snapshot starts after the listed
`StartLocal` anchor and before the first native parse stage.

The native event collector retains subtree ownership and immutable attachment
holds. It has limits of 4096 nodes or entries, 32 tree levels, and 16 MiB of
attachment data and text. A failed addition leaves earlier retained entries
unchanged and refuses later additions. Final native delta preparation applies
its own copied-data limits.

## Verification

The guard fixture passes six groups and eight scenarios. The event collector
passes three groups and six scenarios. The request pipeline passes 16 groups,
including local facts, sidebands, parse-only requests, and absent, closed, or
suspended owners. Each fixture passes its focused address, undefined-behavior,
leak, and stack-use-after-return checks.

All eight affected normal Core target groups also pass. They include the
guard, collector, pipeline, fetcher, native artwork fetcher, local result,
metadata commit, and token-limit fixtures.

The internal and external preparser request mounts remain open. These private
helpers do not change the public preparser API or activate the shared owner.
