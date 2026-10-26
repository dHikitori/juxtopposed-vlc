# TheSubtitleDB portable add-on

This is an executable reference subtitle provider for the new add-on host. The package contains a portable Wasm module; it makes no native Qt calls and imports only `vlc.host_send`. The host owns networking, API-key injection, downloads, candidate matching and attachment. The API-key value never enters the guest.

Install the package with the add-on manager, open its generated settings, and save your own API key from [TheSubtitleDB account](https://thesubtitledb.org/account/keys). A missing key returns `auth_required` without making a network call. The key is required by this integration's setup policy even though TSDB permits anonymous lookup/download routes.

The package searches movie subtitles and individually numbered seasonal episodes. It prefers scoped IMDb/TMDB IDs, supports title lookup, normalizes language codes and returns paginated candidates. Absolute/date numbering and combined-episode files return `unsupported` rather than guessing. Title matches remain evidence for the host to validate; the provider does not claim a hash match. It does not claim music, podcast, episode-title or artwork-provider support.

Asset resolution accepts only subtitle IDs issued by this worker, ignores server-supplied download URLs and constructs the documented API download path. The manifest permits the API and file origins. Authorization is bound only to the API origin and must be stripped on the file-origin redirect. The provider does not request watermark removal or alter attribution.

## Build and test

Requires Rust stable and its `wasm32-unknown-unknown` target. Dependency versions are locked; builds do not execute during package installation.

```sh
rustup target add --toolchain stable wasm32-unknown-unknown
RUSTUP_TOOLCHAIN=stable cargo test --locked
sh build.sh
```

`dist/addon.wasm` is the release artifact. The SDK message ABI is `wasm-message-v1`: `vlc_alloc`, `vlc_dealloc`, `vlc_dispatch` and the single `vlc.host_send` import. Tests use synthetic service fixtures, cover malformed/conflicting replies and cancellation, and never read a developer environment file or make live network requests.

To test that compiled artifact through the actual host worker with synthetic HTTP completions:

```sh
python3 tests/test_worker.py --worker /absolute/path/to/vlc-addon-worker
```

The example lives inside the application repository for development. To distribute it through Git, export its declared package files to a dedicated repository root, set `source` to that repository's approved HTTPS URL, and publish a tag matching `version`. The current source field identifies this example's real development repository; it is not a claim that this nested folder is already a standalone tagged release.

## API verification

The [official API reference](https://thesubtitledb.org/developer/api) was checked on 27 October 2026. The provider uses its `/v1/by-imdb`, `/v1/by-tmdb`, `/v1/by-title` lookup routes with language/offset filters and `/get/{id}` asset resolution. Requests use optional bearer authentication. Public movie and scoped episode lookups returned the documented candidate envelope; a public-domain subtitle download redirected from the API to `https://files.thesubtitledb.org`. Statuses and limits remain service-controlled.

No API keys or subtitle contents are included in this package. The API fixture files are synthetic and contain deliberately hostile URLs to prove that resolution ignores them.
