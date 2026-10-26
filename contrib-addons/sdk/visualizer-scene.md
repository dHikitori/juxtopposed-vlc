# Visualizer scenes

This file describes the first host implementation. The wider architecture also defines future parameter updates from a Wasm worker.

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

The audio callback copies at most 1024 frames from two channels. It does not allocate, wait, run FFT, or call Qt.

The host needs lock-free counters on this callback path. If these counters need locks, the host keeps the artwork fallback.

A separate worker calculates 64 frequency bands, 256 waveform values, RMS, and peak level. It discards old input when its queue is full.

Each analysis frame has a media generation and timestamp. The renderer does not display future frames or frames more than 200 ms old.

A media change, seek, or audio format change clears old analysis and scene buffers. A hidden surface releases its observer.

The media clock controls particle motion. Pause keeps the last image. Reduced motion stops particle motion and feedback, and lowers the update rate.

## Render limits and fallback

The normal render size has a 512-pixel limit on each dimension. The low tier uses 256 pixels. The high tier has a 1024-pixel limit.

The host reduces particle counts at lower tiers. Three slow frames reduce quality. Repeated slow frames at the low tier select artwork.

Audio playback does not wait for scene rendering. Missing analysis, rejected assets, or unsupported output keeps artwork available.

The examples in [visual-scenes](../examples/visual-scenes/README.md) show four scene types. The current controller supports data-only scenes. Worker parameter updates remain a completion gate.
