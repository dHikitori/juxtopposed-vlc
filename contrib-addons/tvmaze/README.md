# TVmaze episode provider

This package supplies episode titles through `video.details`. It uses the portable `wasm-message-v1` ABI. The host owns HTTP requests, media identity rules, field protection, and source credit. The package has no file access or API key.

A request must identify one seasonal episode. The provider first uses a series ID, if one is available. Otherwise, it uses the exact series name and start year. More than one series, season, or episode match causes an `ambiguous` result. Combined episodes and absolute episode numbers cause an `unsupported` result. The provider does not select the first ambiguous match.

The request sequence uses the show, season list, and season episode list. This retains the existing native provider's season rules. The worker retains at most eight valid replies for the current series. A new series clears this cache. The total cached response data cannot exceed 4 MiB. Each result has `episodeTitle`, the response identity, and a TVmaze source link. The host must retain the source credit. See [NOTICE.md](NOTICE.md).

The app can supply this package as a trusted distribution package. This trust comes from the app distribution. It does not prove the identity of a Git publisher. The host preserves an installed version, user disable state, settings, and provider order. A later user-approved Git release can replace a distribution snapshot.

## Build and tests

Use Rust stable with the `wasm32-unknown-unknown` target.

```sh
RUSTUP_TOOLCHAIN=stable cargo test --locked
sh build.sh
python3 tests/test_worker.py --worker /absolute/path/to/vlc-addon-worker
```

`dist/addon.wasm` is the package module. Tests use synthetic HTTP data. They cover identity conflicts, ambiguous replies, cancellation, response limits, and rate limits. They do not use a live service.

On 27 October 2026, a separate live probe passed through the shared owner, add-on manager, package worker, and host network broker. It obtained a public episode title and its source credit. The probe source is `modules/gui/qt/tests/addon_metadata_probe.cpp`. The live result does not guarantee future service access.

The [official TVmaze API](https://www.tvmaze.com/api) defines the routes and data terms. This example uses the app development repository as its source. For a separate Git release, put the complete source and build files at its repository root. Set `source` to that repository. Use a release tag that agrees with `version`.
