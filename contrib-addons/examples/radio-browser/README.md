# Radio Browser source

This add-on lets the user browse radio stations and search by station name. It uses the `discovery@1` SDK and the [Radio Browser API](https://docs.radio-browser.info/).

Enable the add-on in Add-on settings. Open Media sources and select Radio Browser. Open a folder or enter a station name. Play or Enqueue a station. Accepting the add-on's permissions approves its playback; there is no separate step.

The add-on reads at most 10 API records per page and 1,000 records per query. It uses one declared HTTPS origin with one retry. Each API response has a 24 KiB limit. There are at most 16 active requests. The host limits request time and spaces API requests by at least one second per origin.

Station UUIDs are stable item IDs. Pages contain display data only. Queue entries contain stable host references. They contain no stream URL. Each native playback open asks for the station again. The add-on returns the current stream URL only to that resolve request.

The source accepts supported audio codecs and HLS. It removes broken stations, TLS errors, HTTP streams, IP literals, local host names, and unsupported playlist formats. The guest syntax filter has no DNS access. The native host must still validate every DNS address, redirect, and media request against `public-https-media@1`.

API records can change between pages. A page cursor binds the query, offset, and mirror. The source does not keep a station list or a stream URL cache.

Build with the existing stable Rust toolchain and Wasm target:

```sh
RUSTUP_TOOLCHAIN=stable RUSTUP_AUTO_INSTALL=0 ./build.sh
```

Run the offline tests:

```sh
RUSTUP_TOOLCHAIN=stable RUSTUP_AUTO_INSTALL=0 cargo test --locked --offline
python3 -B tests/test_worker.py --worker /path/to/vlc-addon-worker
```

The optional native probe in `tests/native` uses a current Linux host build. It uses a temporary profile, grants source access for that profile, and plays one public station through the native source owner. It verifies a stable queue reference, no resolve before Play, decoded audio, Stop, and joined shutdown. It uses dummy audio output. Link it with a host archive that contains all current source files and generated meta-object code. A mixture of current headers and older compiled files is not a valid probe.

The source URL `https://github.com/hakone-org/addon-radio-browser.git` is a proposed repository. The source and its offline tests are in this directory. See [NOTICE.md](NOTICE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
