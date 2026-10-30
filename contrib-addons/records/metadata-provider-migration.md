# Metadata provider migration

The shared host service owns provider order, identity comparison, account scope, cancellation, and result limits. A portable provider supplies claims. The host decides which claims it can apply. A provider cannot write to a media item.

## Retained features

| Current feature | Separate package | Test evidence | Current core status |
| --- | --- | --- | --- |
| OpenSubtitles search and download | Trusted native adapter in the shared owner | Existing subtitle tests | Retained |
| TheSubtitleDB search and download | `org.hakone.thesubtitledb` | Synthetic tests and a separate authenticated host probe | No service-specific Qt protocol |
| TVMaze episode names | `org.videolan.tvmaze` | Source tests, portable worker tests, public API probe, real episode consumer probe | Old native client retained until consumer parity and cache tests pass |
| MusicBrainz release/recording search and CAA image | `org.videolan.musicbrainz` | Source tests, portable worker tests, public shared-host CAA image probe | Old Lua provider retained |
| Amazon ASIN image | The MusicBrainz package | Synthetic provider branch; public native image adapter probe | Old Lua provider retained |
| French TV channel logos | `org.videolan.french-tv` | All 27 names in source tests; portable worker test; one public shared-host image probe | Old Lua provider retained; several URLs need repair |
| Google Images query rules | `org.videolan.google-images` | Source and portable worker tests; tested public page had no image result | Old Lua provider retained |
| Last.fm album artwork | `org.videolan.lastfm` | Source tests, portable worker test, public shared-host image probe | Old Lua provider retained |

Local filename readers, embedded tags, attachments, folder artwork, and local caches stay in core. A remote replacement must not remove those features. The shipped Lua metadata fetcher directory has no separate provider to port.

## Host image adapter

The app now installs MusicBrainz, Last.fm, French TV, and Google Images as separate packages, with each Wasm module, manifest, license, and notices. Startup seeds a missing distribution package and keeps an existing disabled or removed state. The package suite passes all 24 cases, including complete inventories and repeated startup. The configured install map contains all 20 required files. Distribution upgrades remain a separate open gate; existing core providers remain until their owned route passes preservation tests.

`AddonArtworkAssets` accepts an image claim from an enabled, configured artwork provider. It uses that contribution's installed package, grant, release, account, and settings scope. It forces GET. It discards guest file names and request options.

The host accepts a complete PNG, JPEG, or WebP image of at most 1 MiB. The decoded dimensions must not exceed 1024 by 1024 pixels. It rejects SVG, HTML, incomplete responses, and decode errors. It writes an atomic file in the host cache. The default cache limits are 8192 files and 128 MiB. A full cache stops new files. It does not remove selected files to create space.

The CAA image route can use the closed `internet-archive-images@1` profile. The package and grant must both include it. It permits image GET redirects through `archive.org` and its subdomains over HTTPS/443. The host applies public-address tests and removes credentials. The worker cannot activate this profile for JSON or text requests.

MusicBrainz requests use an app-wide origin interval of at least 1000 ms. This follows the [MusicBrainz API request limit](https://musicbrainz.org/doc/MusicBrainz_API). The [CAA API](https://musicbrainz.org/doc/Cover_Art_Archive/API) defines its front-image redirect route.

## Public observations

These probes ran on 27 October 2026. They do not prove that each item in a service works.

| Route | Result |
| --- | --- |
| Last.fm album page → portable provider → shared host image adapter | Complete 80,349-byte JPEG. The package used the 500-pixel CDN form. |
| MusicBrainz release → CAA → Archive → shared host image adapter | Complete 35,043-byte JPEG. |
| Amazon ASIN image → native host image adapter | Complete 26,523-byte JPEG from `images-na.ssl-images-amazon.com`. The tested release used CAA; the ASIN provider branch used a synthetic response. |
| French TV France Info → portable provider → shared host image adapter | Complete 9,670-byte PNG from the 500-pixel URL. |
| Old TF1 URL | HTTP 400. The tested 500-pixel variant returned HTTP 404. |
| Old Amazon image host | HTTP 403 for the tested ASIN. |
| Google Images page | HTTP 200, with no image tags or image URLs in the bounded response. The page required JavaScript. The old parser also had no result. |

## Remaining cutover gates

The general player and preparser remote artwork routes still need the shared owner. The owned profile must disable direct remote Lua traversal. Owner shutdown must not restore it. Native code must retain local readers. It must not wait for a Qt callback while the Qt thread waits for preparser shutdown.

An existing embedded artwork URL can use HTTP or a private LAN address. Add-on image grants permit public HTTPS. These scopes are different. Keep the existing native behavior until an explicit native policy or scope decision covers its replacement.

New episode cache records use `vlc-host-metadata-v1` with per-field provider credit. The old `vlc-episode-v1` reader remains for legacy observations. A new provider must not need TVMaze IDs or a TVMaze source link. The normal Qt consumer tests must pass before the old native TVMaze client is removed.

## Reproduce the tests

From the repository root:

```sh
RUSTUP_TOOLCHAIN=stable cargo test --manifest-path contrib-addons/sdk/artwork-providers/Cargo.toml --offline
python3 contrib-addons/sdk/artwork-providers/tests/test_worker.py --worker build-redesign/modules/gui/qt/vlc-addon-worker
```

The first command tests the source. The second command runs each packaged portable module with synthetic host responses. Neither command contacts the image services. `modules/gui/qt/tests/addon_artwork_probe.cpp` is a separate optional public-service probe. It has no credential input.
