# Contained static DASH manifests

`AddonStreamManifest::dash` converts a bounded MPD into a document that uses host
proxy URLs for its media resources. This is a restricted host profile. It does
not grant network access or enable production playback.

The concrete URL fields follow the [MPEG MPD schema, sixth edition](https://github.com/MPEGGroup/DASHSchema/blob/6th-Ed/DASH-MPD.xsd).
The byte range model also follows the [DASH-IF timing guidance](https://dashif.org/Guidelines-TimingModel/).
The limits and supported subset below are host design choices.

## Supported document

The MPD must use the DASH namespace and a static presentation. Each representation
must have one explicit `SegmentList` or `SegmentBase`. A list needs a positive
segment duration or a finite `SegmentTimeline` with one entry per listed segment.
A timeline repeat must be nonnegative. Its arithmetic must fit a signed 64-bit
value. An open repeat of `-1` is not supported.

One `BaseURL` can occur at each MPD, period, adaptation set, or representation.
The host resolves this inheritance before it registers resources. It then removes
the source base URLs. A whole-file `SegmentBase` needs an explicit file base URL
on its representation. Its output base URL names one host media route.

The host replaces each `SegmentURL@media` and `Initialization@sourceURL`. A missing
URL is valid only with a declared byte range and an inherited file base. An
initialization resource has the `Initialization` kind. Other media resources have
the `Media` kind. The host records ranges as an offset and length. It keeps the
original inclusive range attributes in the MPD. The proxy must use those original
coordinates for requests and responses; it must not move the start to zero.

Only declared element and attribute names are accepted. Templates, dynamic MPDs,
remote clocks, manifest locations, patches, content steering, content protection,
external indexes, event streams, XLink, XML base attributes, and foreign extensions
are not supported. The host refuses them before the resource callback. It removes
an optional root XML schema location. It does not fetch that schema.

## Bounds and publication

The input must be valid UTF-8 and no larger than 1 MiB. DTDs, custom entity references,
processing instructions, hidden controls, and other document encodings are refused.
The tree has a depth limit of 16, at most 4,096 elements, 16 periods, and 64
representations. An element has at most 32 attributes; an attribute or base URL
has at most 8,192 characters. The resource limit is 512. The output limit is 2 MiB.

Parsing, range checks, timeline checks, and the worst-case output size check occur
before one complete resource batch is registered. Every upstream resource must
use HTTPS, with no user information or fragment. The proxy must still validate
DNS, addresses, redirects, source consent, configuration, cancellation, and each
read. This converter does not perform those checks or open a connection.

The callback must stage the complete batch and return one opaque URL per resource.
Each returned URL must stay under the exact captured loopback origin and token
path. The caller must publish the staged routes only after conversion succeeds.
It must discard the batch if callback results are invalid or conversion fails.
No failure permits the caller to return the source MPD or a source media URL.

## Evidence and remaining work

The combined HLS and DASH suite has 21 cases. It covers inherited bases, separate
representations, private query removal, initialization and media ranges, finite
timelines, malformed input, unsupported external features, size limits, and an
invalid proxy callback. Normal and sanitizer results are recorded with the
implementation checkpoint after execution.

Contained DASH transport, native adaptive playback, seeking, and end-of-input
checks remain separate gates. A general DASH compatibility claim is not valid.
