# Native artwork asset adapter

`AddonNativeArtworkAssets` is a host-only adapter. The app does not activate this adapter yet. The existing native artwork route remains available until the owned metadata pipeline and its format cases pass.

## Authority

The adapter accepts a pending `vlc_metadata_job_t`. It reads the original artwork field from the job's immutable native item copy. It accepts an initial HTTP or HTTPS address only. It refuses a private item policy and a case-insensitive `hakone-addon-source:` item URI before file or network I/O. It has no QML method and no worker message.

A provider result cannot enter this route. All add-on artwork claims must use the package's image broker, even when the target is an ordinary native item. A caller must not create a new native request from an add-on claim to bypass this rule.

This native route retains VLC's HTTP access, account, redirect, and LAN behavior. It does not use the public HTTPS policy for add-ons. The host's native network preference must permit the request. Local files, album caches, embedded attachments, and local readers remain separate native stages.

## Threads and lifetime

The constructor and `resolve` do no file, network, or image-decode I/O. Native worker threads perform those operations. A process-wide limit permits two active transfers or image decodes and 32 retained requests. The request limit includes a native result that waits for a Qt callback. Each adapter uses two native worker threads and one native deadline thread after its first request.

The returned cancellation function can run on a native thread. It stops the native interrupt context, cancels the native metadata job, and releases the adapter's job reference. It needs no Qt event. A cancelled result cannot reach the caller.

Construction requires an app lifetime handle. The constructor admits one counted retirement task before it can start native workers. A closed handle or a failed admission leaves the adapter unavailable. Destruction never submits new joined work.

Workers place complete results in a bounded mailbox. An owner-thread timer delivers those results. No native worker posts to a QObject. The returned cancel function holds a separate native control with a native job, interrupt, and atomic reason. It holds no Work or State reference. A late or blocked cancel cannot retain the URL, pixel data, native read backend, or Qt completion captures after the join. GUI delivery detaches the control's native job. Final retirement also detaches a timed-out job. A retained cancel function cannot keep an extra native backend reference after delivery or join. Cancel applies to pending adapter work; a native consumer retains and cancels its own metadata job or parent anchor before final Apply. Completion captures stay in an owner-thread map. GUI poll and close finish all mutations before they retire those captures outside locks.

`close` revokes its consent epoch, cancels native jobs, and interrupts I/O at once. It does not join on the GUI thread. The admitted retirement task joins the workers and deadline thread, then releases queued results, callback data, and the native read context before its app ticket joins. A blocked native callback keeps the app drain pending. It cannot force teardown of data that it still uses.

The real read backend owns a private VLC child object. Workers retain that context rather than a raw Core instance. Its parent must remain valid through joined cleanup. Core closes its metadata backend before interface waits. The interface retains its event loop through app drain, then releases GUI owners before Core deletes the native parent. Generic `vlc_object_hold` is not a parent reference and is not used.

Call the QObject API on its owner thread. A completion callback can destroy the adapter. It cannot cancel an already admitted native write.

The native I/O deadline is at most ten seconds. The deadline thread interrupts VLC access without a Qt timer. Image codecs do not have a hard CPU-time deadline. A codec can return after the deadline; its result is refused. Shutdown can wait for that native codec call. Codec workspace and Qt renderer allocations are separate from the image pixel limits.

## Consent and publication

A consent epoch binds each request and result. Every actual preference change creates a new epoch. A false-to-true change cannot restore an old result. Close permanently revokes all epochs from that adapter.

A result contains a local PNG file and a host-only `Permit`. It does not change a target item. The native consumer must create its result copy, approve the host file through the native artwork API, and complete the metadata job. It must then call `Permit::admit` around the callback-free `vlc_metadata_job_Apply` operation. It must not call `Complete`, a Qt callback, or a consent setter under that admission lock.

The normal metadata commit validates the original source URI, exact item policy, and artwork revision token before a write. Same-value writes, an A-to-B-to-A change, a new media URI, and a user-selected image all refuse the old patch. The host can supply the artwork replacement bit only for this original native embedded-image operation. A guest cannot supply that bit.

The app must also hide an ordinary raw remote artwork field while the owned profile waits for this adapter. The adapter alone does not provide that publication gate. MainUI owner activation, the preparser handoff, and that gate remain open work.

## Image and cache limits

The native profile accepts at most 32 MiB of encoded input, a dimension of 16,384, and a per-image pixel limit of 128 MiB. Header dimensions receive a conservative eight-byte-per-pixel admission limit before the decoder. Output is one static PNG with a maximum dimension of 4,096 and a 32 MiB encode cap. A bounded output device stops the encoder before it can grow that output buffer past the cap.

The native cache accepts at most 8,192 files and 512 MiB. It uses a content hash for each name and an atomic owner-only file write. It does not evict an image that a caller can still use. Directory scans and cache writes occur on native workers. A cache limit refuses new files. A process-shared file lock protects a bounded scan and each new file admission. A second adapter or app instance must use that same lock before it writes to this cache. A busy cache refuses the new request; it does not wait on the GUI thread.

`AddonArtworkImage` separates two image profiles:

| Profile | Rule |
| --- | --- |
| `StaticAddon` | PNG, JPEG, or WebP only. Refuse animation, compressed PNG metadata, and more than 16 KiB of non-pixel metadata before `QImageReader`. |
| `NativeArtwork` | Accept the installed native codecs after the format checks below. Strip PNG, JPEG, and WebP metadata before decode. Apply JPEG EXIF orientation. Use the default PNG frame or first GIF/WebP frame. Accept self-contained SVG after an XML/resource pass. Inflate SVGZ into at most 8 MiB. |

The SVG pass refuses a DTD, entity references, processing instructions, external `href`, external CSS URLs, CSS escapes, script, foreign content, and animation elements. Fragment references stay inside the document. An inline raster image must pass the native image profile and becomes inline PNG data. Aggregate inline encoded data is at most 4 MiB; aggregate inline pixels are at most half the per-image pixel limit. The XML pass permits at most 8,192 elements, depth 128, 32,768 attributes, and 8 MiB of output XML. The image that leaves the decoder contains pixels only.

Qt SVG can load an image file from an `href` and an external style sheet from a processing instruction. The adapter must therefore validate the XML before the Qt SVG decoder receives it. These behaviors are visible in [Qt6.8.3's SVG handler](https://raw.githubusercontent.com/qt/qtsvg/v6.8.3/src/svg/qsvghandler.cpp).

The installed Qt6.8.3 build reports these 16 reader names: `bmp`, `cur`, `gif`, `ico`, `jfif`, `jpeg`, `jpg`, `pbm`, `pgm`, `png`, `ppm`, `svg`, `svgz`, `webp`, `xbm`, and `xpm`. The adapter checks codec availability before decode. This build has no TIFF, TGA, ICNS, or WBMP reader. The adapter does not claim support for those optional codecs.

The native BMP check bounds the DIB header, dimensions, palette, masks, raster offset, and encoded extent. It removes linked or embedded V5 color-profile references. The ICO/CUR check permits at most 256 entries. Each frame must contain a bounded PNG or RGB DIB. Exact frame aliases reuse one format check. Different frames cannot overlap in the input. The rebuilt container must remain within the encoded limit.

The native PNM check permits P1 through P6. It bounds dimensions, the sample count, and the encoded raster length. It validates ASCII sample values. Binary samples can use eight or sixteen bits. The XBM check bounds header text and raster data. It converts byte and legacy word arrays to a byte array before decode. The XPM check bounds its header, palette, character width, rows, and total text. These formats remain unavailable to guest image assets.

The JPEG check reads only the bounded EXIF orientation field. It accepts either byte order and all eight orientation values. An invalid offset, entry count, type, or value leaves orientation unchanged. The decoder receives no EXIF block. The host applies the orientation to the decoded pixels. A later XMP block cannot reset it. A fixed Adobe color-transform marker can remain for native CMYK decoding. The output contains no image metadata.

The limits apply to encoded input, checked dimensions, and each tracked pixel buffer. They do not state a total process-memory limit. Codec workspace, temporary Qt transforms, SVG effects, and SVG reference expansion need separate limits or isolation. This code does not change the global `QImageReader` allocation setting. Local files, caches, attachments, and user-selected artwork still use their existing native routes until the local preparation stage passes its own tests.

## Focused evidence

The Qt6.8.3 image suite passes 29 cases with no skips. It compares native-profile output with the actual Qt reader. Its format rows use every supported reader name in the installed baseline. Its JPEG cases compare all eight visible orientations in both byte orders. A native CMYK JPEG retains its visible colors. Further cases cover malformed EXIF, eight-bit and sixteen-bit PNM data, aliased and overlapping ICO frames, raster overflow, truncated input, separate guest limits, compressed PNG text/ICC payloads, metadata caps, animation, GIF default frames, SVG resources, spoofed containers, and pixel claims.

The native adapter suite passes 34 cases with no skips against the normal Core library. Its format rows compare cached PNG pixels with the original native reader, including an oriented JPEG. Its authority and lifetime cases cover native snapshot admission, disabled network preference, restricted source items, a real VLC HTTP transfer over loopback and the WSL private IPv4 interface, static local output, real native metadata commits, same-value and A-to-B-to-A writes, a changed source URI, a new user artwork approval, consent epochs, native cancellation, deadline, retained-request and active-I/O caps, cache refusal, and callback destruction. These tests do not establish the still-unmounted preparser route.

The image suite also passes all 29 cases with AddressSanitizer, UndefinedBehaviorSanitizer, and leak detection enabled. Qt and the native Core library use their existing builds. The guest UI asset suite passes 29 cases, and the guest artwork asset suite passes nine cases. Those two regression suites link the existing host archive with the new decoder object. They include real QML pixel output, handle revocation, cancellation, and zero external image loads with positive controls. A normal app build remains a separate integration gate.

## Joined lifetime evidence

The joined lifetime tests cover a blocked native reader, retained
context data, immediate GUI close, completed mailbox withdrawal, closed app
admission, native read exceptions, owner-thread capture release with reentrant
owner destruction, a native cancel that stays active through the app join, and
a retained cancel handle after delivery or timeout. The shared service tests cover a
closed app post gate, an in-flight portable result, and an existing dispatch
which cannot acquire a new lifetime binding. Further cases cover callback and
capture destruction during provider removal, replacement, or shutdown. A native
handler can complete inline after the coordinator records its invocation ID.

The normal native adapter suite passes 34 cases. The normal shared service suite
passes 27 cases. A focused AddressSanitizer, UndefinedBehaviorSanitizer, and leak
detection run passes 12 adapter cases and all 27 service cases. No test fails or
skips, and no sanitizer reports an error. Both changed production sources and
both test sources use the sanitizer. Qt, Core, and the pinned dependency
archives retain their normal builds. The coherent archive hashes, source
hashes, commands, XML results, and logs are in
`/tmp/hakone-artwork-lifetime/`. A normal full app build remains a separate gate.

The service app gate uses memory only. The future native metadata owner must
bind it before native dispatch and close its exact backend before app drain.
Interface withdrawal does not permanently retire the Core instance. Core
cleanup retains that separate authority. A later owner must not be affected by
an old interface's cleanup.

No owner activation or provider removal occurs in this source group. Each
provider family still needs one selected native or portable implementation
before execution, plus a sealed native result and its consumer parity tests.
