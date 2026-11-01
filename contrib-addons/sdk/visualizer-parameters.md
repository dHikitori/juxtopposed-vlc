# Visualizer parameters

A scene can declare bounded numeric fields that a worker can update. The [visualizer scene guide](visualizer-scene.md#parameter-updates) describes the worker side.

Each target node needs a unique `id`. IDs and parameter names start with a letter or underscore and contain at most 64 letters, digits, underscores, or hyphens. Reserved object names are refused. The initial target value must be explicit and inside the declared range.

```json
{
  "contract": "visualizer.scene@1",
  "background": "#000000",
  "nodes": [
    {"id": "bars", "type": "spectrum", "rect": [0, 0, 1, 1], "count": 32, "gain": 1, "color": "#ffffff"}
  ],
  "parameters": {
    "level": {"node": "bars", "property": "gain", "minimum": 0, "maximum": 2}
  }
}
```

An update contains only declared parameter names and numeric values, for example `{"level": 1.5}`. It cannot replace a scene, add a node, change an asset, or supply a URL or shader.

| Target | Property and host range |
| --- | --- |
| Any valid node | `opacity`: 0–1 |
| Spectrum, waveform, spectrogram | `gain`: 0–8 |
| Spectrum | Integer `count`: 1–64 |
| Waveform | Integer `points`: 2–256; `lineWidth`: 0.1–8 |
| Particles | Integer `count`: 1–2,048; integer `seed`: 0–1,000,000; `speed`: 0–2; `size`: 0.001–0.1 |
| Group | `rotation`: −360–360; `scale`: 0.1–4 |
| Feedback | `decay`: 0–0.99; `scale`: 0.5–1.5 |

A declaration can narrow its host range. It cannot widen it. Two names cannot target the same node/property pair. A scene has at most 64 declarations. An update has at most 64 entries and 16 KiB of encoded JSON.

Updates are atomic. Unknown names, incorrect types, out-of-range values, or a combined particle count above 2,048 leave the output unchanged. The full scene validator must run before this numeric layer; parameter validation alone does not validate the complete graph.
