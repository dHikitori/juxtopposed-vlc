# Visualizer scenes

This file describes the host scene contract. A scene can use static values or bounded parameter updates from a Wasm worker.

A visualizer contribution uses `visualizer@1`. Its `scene` field names a declared JSON file. Its `analysisChannels` lists `bands`, `waveform`, or `levels`.

The package must request the same channels in `audio.analysis`. A visualizer package cannot request network or background-session permissions.

The user selects one visualizer for the music surface. The host supplies colors, the media clock, and bounded audio analysis. A scene cannot open a file, request a URL, or run code.

## Document

```json
{
  "contract": "visualizer.scene@1",
  "background": "theme.background",
  "nodes": [
    {
      "type": "spectrum",
      "count": 32,
      "rect": [0.05, 0.1, 0.9, 0.8],
      "color": "theme.accent",
      "gain": 2
    }
  ]
}
```

The host rejects unknown fields. The document can contain at most 256 nodes and eight nested groups.

All numbers must be finite. The optional `rect` contains normalized x, y, width, and height. The host clips each node to its rectangle.

`opacity` is optional. Its range is zero to one. Colors use `theme.background`, `theme.foreground`, `theme.accent`, `#RRGGBB`, or `#AARRGGBB`.

## Nodes

| Type | Fields and limits |
| --- | --- |
| `spectrum` | `count`: 1–64; `color`; optional `gain`: 0–8. |
| `waveform` | `points`: 2–256; `color`; optional `gain`: 0–8 and `lineWidth`: 0.1–8. |
| `spectrogram` | `color`; optional `gain`: 0–8. The host keeps 64 rows of recent bands. |
| `particles` | `count`: 1–2048; `seed`: 0–1000000; `speed`: 0–2; `size`: 0.001–0.1; `color`. |
| `gradient` | `vertical`: Boolean; `stops`: 2–8 position/color pairs in increasing order. Positions use the range zero to one. |
| `group` | `children`; optional `rotation`: −360–360 degrees and `scale`: 0.1–4. |
| `feedback` | `buffer`: 0 or 1; `decay`: 0–0.99; optional `scale`: 0.5–1.5. |
| `texture` | `asset`: a key in the document's `assets` object. |

The particle limit applies to the complete scene. Each feedback buffer can occur once. The host owns both buffers.

## Images

An optional `assets` object maps at most 16 asset keys to declared package files:

```json
"assets": {"tile": "images/tile.png"}
```

The host accepts PNG, JPEG, and WebP. Each input file has a 1 MiB limit. Each decoded image has a 1024 × 1024 pixel limit.

The complete set of decoded images has a 16 MiB limit. The scene receives decoded images through opaque keys. It receives no native path.

## Audio and time

The host calculates 64 frequency bands, 256 waveform values, RMS, and peak level from the playing audio. It drops old audio when it is busy.

Each analysis frame has a media generation and timestamp. The renderer does not display future frames or frames more than 200 ms old.

A media change, seek, or audio format change clears old analysis and scene buffers. A hidden surface releases its observer.

The media clock controls particle motion. Pause keeps the last image. Reduced motion stops particle motion and feedback, and lowers the update rate.

## Render limits and fallback

The normal render size has a 512-pixel limit on each dimension. The low tier uses 256 pixels. The high tier has a 1024-pixel limit.

The host reduces particle counts at lower tiers. Three slow frames reduce quality. Repeated slow frames at the low tier select artwork.

Audio playback does not wait for scene rendering. Missing analysis, rejected assets, or unsupported output keeps artwork available.

The host renders one frame at a time and skips frames when it is busy. A new timeline clears the feedback buffers.

The Visual scenes example package shows four scene types.

## Parameter updates

A worker can change only numeric fields that the scene declares. Give the target node an `id`. Add the parameter to the scene's `parameters` object:

```json
{
  "contract": "visualizer.scene@1",
  "background": "theme.background",
  "nodes": [{"type": "spectrum", "id": "bars", "count": 32, "gain": 1, "color": "theme.accent"}],
  "parameters": {"gain": {"node": "bars", "property": "gain", "minimum": 0, "maximum": 4}}
}
```

The declaration has exactly four fields. The node must have the property and a valid initial value. The range cannot exceed the node's normal limits. Each node ID and parameter target must be unique. IDs use an ASCII letter or underscore first, then letters, digits, underscores, or hyphens. The limit is 64 characters. The names `__proto__`, `prototype`, and `constructor` are reserved.

The host accepts at most 64 parameters. The permitted properties are `opacity`; spectrum `count` and `gain`; waveform `points`, `gain`, and `lineWidth`; spectrogram `gain`; particle `count`, `seed`, `speed`, and `size`; group `rotation` and `scale`; feedback `decay` and `scale`. Integer properties remain integers. The complete scene keeps the 2,048-particle limit after each update.

Declare `updateParameters` in the contribution's `operations`. Declare a `wasm-message-v1` runtime and its file in the manifest. The host starts that runtime from verified package bytes. A data-only scene needs no runtime.

An invocation uses the normal worker envelope. Its context has these fields:

| Field | Content |
| --- | --- |
| `contract` | `visualizer.parameters@1` |
| `session` | A host session reference |
| `generation` | A decimal string. A new timeline invalidates old requests. |
| `parameterSchema` | The declared parameter map |
| `clock` | `mediaTimeUs`, `rate`, and `playing` |
| `analysis` | Only the approved and declared channels: 64 `bands`, 256 `waveform` values, or `levels` with `rms` and `peak` |
| `viewport` | The bounded output `width` and `height` |
| `theme` | Host `background`, `foreground`, and `accent` colors |
| `reducedMotion` | `false`; the host stops the worker when reduced motion applies |

The worker receives no PCM samples, media paths, or API key bytes. Nonsecret settings and credential presence flags use the normal worker fields. The visualizer host supplies no network, private storage, WebSocket, or core operation adapter.

Return a normal `result` with status `match` or `partial`. Its data has one field:

```json
{"parameters": {"gain": 2}}
```

The host applies a valid partial update as one operation. An unknown parameter, a value outside its range, or a nonnumeric value rejects the complete update. The response data has a 16 KiB limit.

The host permits one request at a time and at most ten requests per second. The first request has a two-second deadline. Later requests have a 500 ms deadline. Three failed or invalid requests stop parameter updates; the static scene continues. A host worker slot refusal can retry without changing the scene. Parameter values expire after two seconds without a valid result.

Hide, close, a new timeline, or a change to the package, its settings or its permissions cancels old work.

## Package changes

The host loads the scene, the module and the images from the verified package
in one step. An update, a settings change, a permission change or turning the
add-on off makes that loaded scene invalid.
