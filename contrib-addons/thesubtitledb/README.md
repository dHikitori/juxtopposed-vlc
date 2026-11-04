# TheSubtitleDB portable add-on

This subtitle provider uses a portable Wasm module. It imports only `hakone.host_send`. The host owns HTTP requests, API-key use, downloads, identity rules, and track attachment. The API-key value does not enter the guest.

Install the package with the add-on manager. Open its settings. Save your own key from [TheSubtitleDB account](https://thesubtitledb.org/account/keys). A missing key causes `auth_required` before an HTTP request. This host profile needs a key. The service also has public routes.

The package supplies movie subtitles and subtitles for one seasonal episode. It uses scoped IMDb or TMDB IDs when they are available. It also supports title search, language codes, and result pages. Absolute episode numbers, date numbers, and combined episodes cause `unsupported`. The host must validate title evidence. This provider does not claim a file hash match. It does not supply music, podcast, episode-title, or artwork metadata.

Asset resolution accepts only subtitle IDs that this worker supplied. It does not use download URLs from an API response. It constructs the documented download path. The manifest permits the API and file origins. The host sends Authorization only to the API origin. It must remove Authorization on a redirect to the file origin. The provider does not request watermark removal or change attribution.

## Build and tests

Use Rust stable with the `wasm32-unknown-unknown` target. Dependency versions are fixed. The host does not run a build at installation.

```sh
rustup target add --toolchain stable wasm32-unknown-unknown
RUSTUP_TOOLCHAIN=stable cargo test --locked
sh build.sh
python3 tests/test_worker.py --worker /absolute/path/to/hakone-addon-worker
```

`dist/addon.wasm` is the package module. The message ABI is `wasm-message-v1`. Its exports are `hakone_alloc`, `hakone_dealloc`, and `hakone_dispatch`. Tests use synthetic data. They cover invalid replies, identity conflicts, cancellation, and asset resolution. They do not read a developer environment file or use a live service.

The package source is in the app development repository. For a separate Git release, put the complete source and build files at its repository root. Set `source` to that repository's approved HTTPS URL. Use a tag that agrees with `version`. The host installs only the declared package files. The current source field does not claim a separate tagged release for this folder.

## API evidence

The [official API reference](https://thesubtitledb.org/developer/api) was examined on 27 October 2026. The provider uses `/v1/by-imdb`, `/v1/by-tmdb`, and `/v1/by-title` with language and offset filters. Asset resolution uses `/get/{id}`. Public movie and episode requests returned the documented result format. A public-domain subtitle download used a redirect from the API origin to `https://files.thesubtitledb.org`. The service controls its limits and response status.

This package contains no API key or subtitle content. All API fixtures are synthetic. Some fixture URLs are invalid by design. Tests prove that the provider does not use those URLs for asset resolution.
