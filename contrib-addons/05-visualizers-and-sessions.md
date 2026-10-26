# Visualizers and collaborative sessions

These designs exercise the platform beyond metadata and forms. They define contracts and feasibility gates; they do not implement a visualizer or watch-party feature.

## Visualizer contribution

A `visualizer` contribution declares a display name, preview asset, `visualizer@1` contract, required analysis channels, portable scene resource, supported quality tiers and reduced-motion alternative. Selecting it through the host's visualizer picker grants `audio.analysis` for that active visualization only. A pure declarative scene needs no worker; optional worker logic can update bounded scene parameters.

Use one host controller for current built-in visualizers and new contributions. There is one active visualization per playback surface. Core native visualizers can be trusted adapters; existing projectM/goom/native plugin packages do not automatically become compatible portable add-ons. V1 focuses on music visualization, not arbitrary video filters, audio DSP or capture.

## Audio analysis and rendering path

```text
decoded audio callback
    -> bounded copy/aggregation into a host ring buffer
    -> host analysis worker: FFT, bands, envelope and waveform
    -> timestamped latest analysis frame
    -> host scene renderer at the presentation clock
```

The existing player exposes pre-volume decoded samples, but the callback runs on the audio thread and its block/format cannot be retained. Copy only a measured, bounded amount into preallocated storage. Never allocate unbounded memory, perform FFT, call Wasm, serialize messages, wait for IPC or take UI locks in that callback. On overload, drop visualization input, not playback data.

Analysis output contains media epoch, sample/analysis timestamps normalized to media presentation time, sample rate/channel summary, host-defined FFT bands, levels and bounded waveform samples. Expose analysis summaries, not a raw PCM recording interface. The broker sends no analysis to inactive add-ons. V1 manifest validation rejects network permissions anywhere in a portable visualizer package, including optional ones, and rejects background-session activation while it visualizes. This prevents analysis from being sent through broker networking; a package mixing a networked service with visualization must split its visualizer into a separate package or wait for a separately reviewed cross-capability profile. This package-level restriction matches the shared-worker trust model.

Playback clock/PTS controls visual progression, including pause, seek, rate change and discontinuity. A worker wall clock cannot be the presentation clock. Host scheduling chooses when frames appear and drops late visual frames. Clear old analysis on media epoch changes, device resets and discontinuities; do not display the previous song's waveform on a new track.

## Portable scene contract

Define a bounded visual scene/effect graph rendered by trusted host primitives: spectrum bars, polylines/waveforms, gradients, transforms, bounded particles, textures and approved compositing/feedback effects. Parameters may reference analysis channels, theme tokens and a host-supplied media clock. Provide host oscillators/envelopes so routine animation does not require per-frame worker execution. Static assets use the normal validated package/asset path.

Graph schemas are typed and validated ahead of use: known nodes, finite numbers, bounded counts/dimensions and no arbitrary recursion or dynamic code. Feedback is permitted only through an explicit host-owned bounded buffer primitive. A guest may send validated uniform/parameter patches at a limited rate; it cannot access Qt scene objects or GPU handles. GPU translation is performed by the host's vetted implementation for each rendering backend.

Do not accept arbitrary GLSL, SPIR-V, `.qsb` blobs or native renderer libraries in the portable v1 profile. Wasm isolation does not contain a malicious shader or a driver hang. A future custom-shader tier needs its own validation, compilation, platform and performance design. The first graph should still support visibly different spectrum, oscilloscope, spectrogram and particle styles; validate those examples before declaring the contract expressive enough.

Initial prototype targets: latest-only analysis delivery at up to 30 Hz, a 30 FPS default visual render target, one active graph, at most 256 scene nodes, 2,048 particles and two 1,024-square feedback textures. Measure CPU/GPU/memory use and adapt quality; these are initial host policies, not a fixed performance guarantee. Hidden/minimized surfaces pause rendering or use a much lower rate. Reduced motion selects a static/low-motion tier; a host low-cost fallback is always available.

A slow worker cannot block rendering: keep last valid parameters and expire them after a bounded interval. An over-budget graph degrades quality or falls back to built-in artwork. Device loss or unsupported GPU capability uses a fallback tier, never interrupts audio. Keep player chrome/subtitles outside the visualizer's replaceable scene.

Lifecycle events: selected, viewport/scale/theme changed, audio format changed, media epoch/discontinuity, play/pause/rate, hidden/resumed, device lost and deselected. On deselection, release analysis subscriptions, scene buffers, worker jobs and temporary resources. Every event is asynchronous and generation-scoped.

## General extensions

General behavior comes from the same small APIs used by other contributions: commands, typed events, host views, timers, private storage, HTTP/WSS and explicitly granted playback/queue operations. Do not add an unrestricted “general extension” API exposing the application object tree.

Use a `session.controller` registration for opt-in long-lived behavior such as watch parties, listening rooms or queue automation. It declares required observations/commands, a session UI and end behavior. The host issues the session ID and leases after user activation. Mere installation, metadata search or opening an unrelated route cannot start a control session.

## Worked design: watch with friends

The extension provides:

- A `ui.command` named `org.example.watch/join` offered in player customization/overflow.
- A `ui.modal` for create/join and a session status view using standard components.
- Optional account settings through the account broker.
- A `session.controller` for room lifecycle and synchronization logic.
- Declared HTTPS/WSS relay origins, `playback.observe`, `playback.control`, and `session.background`; optional queue operations only when shared queues are explicitly supported.

### Join and consent

1. The user opens Join/Create room and chooses a relay/room. V1 supports outbound WSS relays; arbitrary inbound sockets, LAN peer discovery, WebRTC media transport and local endpoint grants are future contracts.
2. The host shows which identity information will be shared and asks for session-specific playback authority. Joining a room and permitting it to control playback are separate choices; an observe-only mode remains possible.
3. The host creates a session and a renewable playback-control lease bound to this package, room session, current media policy and permitted operations. It establishes the approved relay connection through the network broker. Room credentials/tokens follow the normal secret rules.
4. The extension exchanges room state and identity evidence. Each participant resolves and plays their own authorized copy/stream. Do not transmit local paths, access tokens or media bytes. A room identifier or optional scoped fingerprint can be shared with consent. No screen/audio capture or media relay is implied.
5. If media editions/durations differ or identity cannot be confirmed, require a visible choice/degraded mode; a matching title alone cannot authorize a remote media switch.

### Timeline snapshots

Expose copied `playback.observe` snapshots rather than polling a displayed position label:

```text
media epoch and safe shared identity reference
observation sequence and local monotonic observation timestamp
media position in integer microseconds, or explicit unavailable state
rate, paused/playing/buffering state
duration, seekability, supported rate range and live seek window
timeline discontinuity generation
optional host-estimated output latency and its uncertainty
```

Normalize core sentinel timestamps to explicit nullable/enum states. Do not expose raw pointers or timer internals. Native timer callbacks run without the player lock and forbid player calls: copy their values and enqueue observation work without calling back into the player. Separately, ordinary player/playlist listeners that run under core locks must defer dispatch until those locks have been released.

The extension estimates network offset and drift through its room protocol. It cannot directly compare monotonic timestamps from different machines. Host adapters provide local monotonic time and consistent units; the extension maps remote state into a local target and uncertainty range. The host may expose output latency only where it has a meaningful estimate.

### Commands and arbitration

Permitted commands include play, pause, seek, bounded temporary rate correction and optional execution at a host-local monotonic deadline. Each request carries session/lease, media epoch, expected transport revision, command ID, origin sequence and target parameters. The host rechecks all of them on execution. Seekability, live ranges and supported rates are current-media capabilities, distinct from installed API support or user grants.

Use an exclusive remote-control lease per player initially. If multiple extensions want control, require an explicit user switch. Core keyboard/mouse commands always take priority: manual intervention suspends remote control until the user resumes synchronization or chooses another documented session policy. Remote state must never immediately undo the user's pause or seek in a feedback loop.

Keep command provenance and sequence numbers so an observed remote-origin seek is not echoed as a new local action. Reject stale/duplicate commands. Revalidate media epoch at scheduled execution time and cancel pending commands on media switch, disconnect, permission revoke, update or session end. Do not automatically change volume or output devices; those need different authority.

Restore a temporary rate only if the current rate is still owned by that session's latest correction; do not overwrite a later user change. An expired/disconnected session stops accepting commands, returns to normal host ownership and shows reconnect/end controls. Core End session remains reachable outside the extension modal/replacement UI, even when its worker has crashed.

### Expected limits

This design supports coordinated playback, not guaranteed frame- or sample-accurate output. Network jitter, buffering, different editions, output device latency and engine seek/rate limits affect the result. Nonseekable/live sources may permit only coarse synchronization or observe-only mode. On large uncertainty or conflicting media, show the reason and offer resync/manual control rather than thrashing playback.

Before implementing the feature, prototype two-player drift measurement, seek and bounded-rate correction with explicit output-latency uncertainty. The platform work is complete only when late commands, disconnects and competing local input are reliably contained. The room protocol and relay service are extension/backend work, not a reason to expand unrestricted core permissions.

## Cross-cutting acceptance scenarios

| Scenario | Required outcome |
| --- | --- |
| Visualizer worker hangs or sends excessive updates | Audio continues, bounded queues drop/coalesce updates, visual fallback appears |
| Seek/track switch during analysis | Old media epoch is discarded and scene time resets correctly |
| Pause, minimize, reduced motion or GPU loss | Host reduces/stops rendering and provides a safe supported fallback |
| Room sends duplicate/stale/future-dated commands | Host rejects them or cancels them when their scoped deadline/epoch expires |
| User pauses while a room is playing | Local command wins and remote control visibly suspends |
| Extension disconnects during rate correction | Its correction is released only if it still owns that rate state |
| Worker crashes while custom Home is selected | Built-in Home and End session remain accessible without the worker |
| Add-on update arrives during a room session | Defer or explicitly end/quiesce the session before switching release generations |
