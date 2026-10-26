# Google Images provider

This package retains the old Google Images query rules in a portable worker. It can supply artwork for a radio or TV listing, an artist and album, or an artist and track title. It reads a bounded HTML prefix. It does not run page scripts. Each result has label evidence and a source link. The host must retain image ambiguity for user selection.

The public probe on 27 October 2026 returned an HTTP 200 page that needs JavaScript. It had no image tags or image URLs within the read budget. The old Lua parser also found no image for that probe. This result does not prove that every query fails. It also does not prove a working live replacement. Keep the old core path until a working replacement or an explicit scope decision permits its removal.

Use Rust stable. Run `cargo test --locked` in `contrib-addons/sdk/artwork-providers`. Then run `sh build.sh` here. The package module is `dist/addon.wasm`. The shared SDK source is at `../../sdk/artwork-providers`. A separate Git release must include that complete source and a correct Cargo path. Set `source` to the release repository before distribution.
