# Static UI images

Image delivery also checks a shared in-memory store token. The host captures it before background admission. A settings, release, grant, recovery, or removal write invalidates that token before file publication. Cold delivery, warm-cache delivery, provider reads, and trusted pixel snapshots refuse an old token. A short native scope sweep withdraws already mounted images. Publication reads no files on the UI thread.

The token covers mutations by owners in the same process. External file edits remain subject to background state checks and the Manager's live session epoch; this is not an instantaneous cross-process revocation claim.

A UI route, modal, section, or replacement can declare static package images. Include each image in the manifest file list.

```json
{
  "id": "stations",
  "type": "ui.section",
  "contract": "ui@1",
  "hook": "home.sections@1",
  "view": "views/stations.json",
  "assets": {
    "cover": {"path": "images/cover.png"}
  }
}
```

The `assets` object maps an identifier to an object with one `path` property. The path must name a declared package file. Paths, URLs, and image bytes cannot come from a state update.

Use the alias in the UI document:

```json
{"type": "Image", "artwork": "asset:cover", "text": "Station cover"}
```

An artwork binding can select an alias from the declared map. The host supplies the image ticket and the image-owner claim. A package cannot create these values. A path or URL in `artwork` does not load an image.

The host accepts static PNG, JPEG, and WebP images. The file suffix must agree with the content. PNG and WebP animation are not permitted. Compressed PNG text and color profiles are not permitted. Container metadata has a 16 KiB limit. SVG, fonts, scripts, external image references, and other file types are not permitted. The host decodes the image and keeps pixels without the file metadata. Qt receives pixels through the host image provider. It does not reopen the package file.

WebP support needs the Qt Image Formats WebP plugin at runtime. A missing codec keeps the placeholder. The host does not select a different decoder for that file.

| Limit | Maximum |
| --- | --- |
| Encoded image | 1 MiB |
| Container metadata | 16 KiB |
| Source dimensions | 1024 by 1024 pixels |
| Output dimensions | 512 by 512 pixels |
| Image declarations per contribution | 64 |
| Encoded images per contribution | 8 MiB |
| Queued and active encoded data across services | 8 MiB |
| Integrity capture jobs across services | 1 |
| Decode jobs across services | 2 |
| Retained decoded cache per service | 32 MiB |
| Resource tickets per service | 256 |
| Mounted image owners per session | 32 |
| Mounted image owners across services | 64 |

The host reserves pixel memory for active decode jobs. This reservation and the encoded-data and cache quotas do not include codec workspace or Qt render allocations. Do not interpret the cache quota as a limit on all process memory.

Package reads and integrity checks run on a background thread. The existing package inventory limit is 64 MiB. The file reader can hold one additional file of at most 16 MiB before it rejects a package above that limit. These temporary inventory buffers are separate from the encoded-image queue. A decode checks the captured package state, settings, and permissions on its background thread. It does not read the full file inventory again. The UI publishes pixels only for the current session generation.

Each mounted image needs a claim from its actual view session. An image-owner claim ends when its owner is destroyed. Session close, suspension, package withdrawal, safe mode, release changes, account changes, and permission changes revoke tickets and claims. Resume supplies fresh tickets. A late decode cannot publish an image into an old session. Service destruction does not wait for a decode; its job and byte reservations remain active until the work and result handover end.

An image that is missing, incorrect, or above a limit keeps its placeholder. Image failure does not give a package more access. The host does not use Qt image caching for these images.

Themes continue to contain tokens only. Network artwork uses a separate metadata adapter. This image map does not give a UI view network access. The existing manifest `icon` path is separate from this view image map; UI `icon` values still select fixed host icons.
