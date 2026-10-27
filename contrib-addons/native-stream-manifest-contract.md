# Native stream manifest conversion

`AddonStreamManifest::hls` converts HLS resource references to host proxy URLs. It performs no network request. It grants no source, credential, DNS, redirect, or playback authority. The contained transport and its native playback tests remain open; this converter does not enable production Play or Enqueue.

The HLS URI, key, and byte-range syntax comes from [RFC 8216, sections 4 and 5](https://www.rfc-editor.org/rfc/rfc8216.html#section-4). This implementation has a closed compatibility profile. It is not a complete HLS semantic validator. VLC must still validate and decode the media.

## Input and output limits

- Input is at most 1 MiB, with at most 8,192 lines and 8,192 characters per line.
- UTF-8 must be valid. A byte-order mark, hidden control characters, and a second format header are refused. CRLF is accepted.
- The profile accepts declared HLS versions 1 through 7. Variable substitution, low-latency tags, and unknown extension tags are refused.
- Each resolved resource URL is at most 8,192 encoded bytes. At most 512 references and 4 MiB of expanded URL text can enter one batch.
- Output is at most 2 MiB. It contains only host URLs at registered URI positions.

URI resolution uses the final, verified upstream response URL. The caller must supply that URL after redirect validation. Resource addresses must use HTTPS and have no user-info or fragment. The transport must separately enforce public addresses, DNS pinning, TLS, redirects, source authority, and cancellation before I/O.

## Resource registration

| HLS reference | Host resource role |
| --- | --- |
| Variant, alternate rendition, or I-frame playlist | Playlist |
| Media segment | Media |
| Initialization map | Initialization |
| AES-128 identity key | Key |
| External session data | Session data |

The converter parses the complete input before it calls the host registration callback. That callback registers the complete batch or no resources. Registration itself must start no outbound I/O. It must check the source's current native authority. The caller owns the staged batch and must discard it on conversion failure, including an invalid URL returned by its callback. It can publish the batch only after the complete result is valid.

The callback returns one opaque URL for each resource. Every URL must use the exact supplied loopback address, port, and token prefix. The remaining path uses bounded letters, digits, underscores, hyphens, and path separators. User-info, queries, fragments, dot segments, and alternate origins are refused. These constraints match the native proxy policy.

The proxy must keep original addresses and signed queries on the host. It must never return them as a redirect to the native player. Each child playlist needs the same conversion before delivery. The player must never receive an original media, key, initialization, or session-data URL in a URI position.

## Byte ranges and keys

The host resource carries its original absolute byte range. Addition must not exceed the signed 64-bit limit. An implicit segment offset needs a preceding range on the same upstream resource; the output makes that offset explicit. This keeps the offset correct when separate ranges receive separate opaque URLs.

Initialization map ranges require an explicit offset in this profile. I-frame-only media playlists require an explicit initialization map. AES-128 uses the identity key format and version 1. Other encryption profiles remain unsupported. The transport must enforce the registered range and its response bounds; conversion alone does not enforce a transfer.

## Evidence and remaining work

Fourteen cases pass in normal and ASAN/UBSAN builds, including stack-lifetime checks. Cases cover relative resources, private queries, nested playlist roles, keys, map attribute order, implicit segment offsets, malformed input, resource limits, wrong proxy scope, empty live playlists, and hidden URI lines. Registration receives no partial batch after a parse failure.

These are conversion tests. They do not establish actual media playback, adaptive reload, seek, DNS containment, redirect handling, key transfer limits, cancellation, or child-stream EOF behavior. DASH conversion and the contained transport remain required work.
