# Native cover image choices

This path handles an explicit user file choice. It has no guest command, QML
operation, or serialized approval token. A raw file URI cannot create a receipt.
The existing local, cache, and attachment discovery routes remain available.

## Preparation and preview

`ArtworkSelection` captures the exact native target before file I/O. It creates a
host-local metadata anchor with `vlc_metadata_job_StartLocal`. The worker receives
held native item and job references, copied paths, and the app lifetime handle.
It receives no QObject or raw Core instance.

Joined Qt work reads the selected local file, validates its container, applies
native orientation, and writes a private normalized PNG. The result includes a
bounded pixel preview and a private `Receipt`. A GUI timer reads a native mailbox
and delivers that result. A worker never calls a UI callback or posts to a
QObject. Closing the owner cancels its anchors and clears its GUI callbacks
without a wait for file or codec work. The app barrier still joins that work
before Qt destruction.

A new choice, a changed target, or Cancel invalidates the old receipt. Preview
alone does not change the live artwork field. The original selected file is not
read again at Save. A later change to that file cannot replace the prepared
pixels.

`ArtworkNativePreview` handles an artwork path already admitted by the native
consumer. It uses the existing `ArtworkStore::read` reader on joined work. It
does not approve a file or replace the native local/cache/attachment routes with
a guest profile. The widget rechecks the native display URI before it paints a
result. Both helpers use the same process decode budget.

## Native selected-file limits

The selected-file profile is separate from guest assets and native remote
artwork. It permits at most 512 MiB of encoded input, a dimension of 32,768, and
100 million input pixels. These dimension and pixel bounds match the existing
native artwork reader. The host asks the installed Qt reader for scaled output.
Some readers use a full internal frame before scaling. The closed native codec
set can use up to eight bytes per input pixel for that frame. This bound does not
include codec workspace. The host does not change Qt's global image allocation
setting.

The file reader allocates one buffer with the captured file size. It refuses
a truncated or growing file. Encoded contents can reach 512 MiB. Container
normalization can keep a second encoded payload of at most 512 MiB. Some
containers share the input buffer instead. Qt can reserve extra array capacity,
and a container parser can keep intermediate entries. The scaled reader output can use
128 MiB at eight bytes per pixel. Conversion and the fresh pixel copy each
use at most 64 MiB. These are separate buffer bounds, not a claim that the
complete codec has this memory limit. A codec can also retain a full native
frame and workspace. A full frame has the 100 million pixel admission bound.
The host does not impose a new global Qt allocation limit.

Normalized output has a maximum dimension of 4,096. A bounded encoder permits
at most 80 MiB. This includes a high-entropy 4,096 by 4,096 RGBA image and PNG
container overhead. If a valid image does not fit that encode cap, the host makes one
second attempt at a maximum dimension of 2,048. Preview has a maximum dimension
of 512. One selected or native-preview decode can run across the process. At most
16 requests or receipts can remain. A cancelled worker keeps its request permit
until its captured native work is released. The shared app thread limit remains
held through both thread joins and Qt thread-storage cleanup.

The cache permits at most 8,192 entries and 512 MiB. A content hash names each
PNG. An atomic file write uses owner-only read/write permissions. Cache scans
and writes run on the worker under the shared cache lock. The host does not
evict a file that a live item can still display. A full cache refuses a new image.

The installed format baseline and metadata/resource checks are specified in
[native artwork assets](native-artwork-materializer-contract.md). The guest
PNG/JPEG/WebP profile and native remote profile keep their previous limits.
Optional codecs absent from the installed build have no preservation claim.

## Save and cancellation

A receipt can stamp the tag writer's private native copy. It accepts only the
captured source URI and exact item policy. The host validates the target and
selection generation before the existing native tag write. The callback-free
native current query also compares native source revisions. A source that
changes and then returns to its earlier address cannot reuse a receipt.
After that write,
`Receipt::apply` uses the existing native metadata comparison and replacement
operation for artwork. The host applies live title and identity edits afterward.
This order keeps those edits from invalidating the artwork anchor first.

The native comparison rejects a stale artwork token, a same-value write, changed
identity, changed source, revoked policy, cancelled anchor, or wrong target.
The receipt permits one application attempt. Cancel does not change the live
item. A new selection is necessary after a stale-result refusal.

The installed native tag writer keeps its existing format and image-size
limits. Preparation does not guarantee that every file format can embed the
image. The existing native tag writer is not a filesystem transaction. If another
operation changes the target during that write, the file tags can be written
while the live artwork comparison refuses. The UI reports that exact outcome.
A failed tag write leaves the live item and user edits unchanged. This path does
not claim atomicity between a media file write and the native metadata state.

## App ownership

Every helper uses `MainCtx::addonStreamLifetime()`. The host connects
`addonWorkRetiring` to `close` before it starts the app drain. Native cancellation
uses only held native state and standard synchronization. GUI owners and queued
payloads must die before `QCoreApplication`. The shared barrier covers remaining
Qt file, image, and thread-storage cleanup. A timeout keeps the app alive.

The existing native tag writer retains its current execution path. This change
moves selected-file preparation and cover preview off the GUI thread; it does
not add a new background tag writer or retain a raw Core instance on a worker.

## Validation

The selected-image path passes 23 normal and 23 sanitizer cases. The native
selection helper passes 14 cases in each build. The sanitizer runs enable
address, undefined-behavior, leak, and stack checks. Diagnostics are empty.
The cases cover installed image formats, large native files, orientation,
prepared pixels, stale targets, cancellation, and retained receipts.

The production consumers and their current tests pass syntax checks with the
normal Qt flags. The 15-case actual consumer run still needs the fresh normal
Qt module. Full app and supported-platform checks remain open.
