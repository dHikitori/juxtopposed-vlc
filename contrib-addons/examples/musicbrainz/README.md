# MusicBrainz artwork

This portable package retains the MusicBrainz, Cover Art Archive, and Amazon ASIN paths from `00_musicbrainz.lua`. The old core provider remains until the replacement passes its tests.

The package accepts a scoped MusicBrainz release ID. If the ID is absent, it searches by artist and album, or artist and recording title. It compares the available labels. It retains up to eight matching releases. Multiple usable images require a user choice. It supplies artwork only.

The host limits MusicBrainz requests to one start per second for the complete app. The host downloads and decodes the image. The worker cannot activate the Archive redirect profile. [MusicBrainz API](https://musicbrainz.org/doc/MusicBrainz_API) and [Cover Art Archive API](https://musicbrainz.org/doc/Cover_Art_Archive/API) define these services.

On 27 October 2026, the shared host route obtained release data from the public API. It followed the CAA and Archive image redirects and decoded a 35,043-byte JPEG. The old Amazon image host returned HTTP 403 for ASIN `B000001FI7`. The same image path returned JPEG data from `images-na.ssl-images-amazon.com` and `m.media-amazon.com`. The native image adapter also decoded a 26,523-byte JPEG from the first of those hosts. The ASIN provider branch has a synthetic test; the live release used CAA. These results do not prove that each release has an image.

Build from this source tree:

```sh
RUSTUP_TOOLCHAIN=stable cargo build --target wasm32-unknown-unknown --release
cp target/wasm32-unknown-unknown/release/vlc_addon_musicbrainz.wasm dist/addon.wasm
```

The path dependency is `../../sdk/artwork-providers`. A separate release repository must include that source or use a fixed published SDK version. Do not run a build script during installation.
