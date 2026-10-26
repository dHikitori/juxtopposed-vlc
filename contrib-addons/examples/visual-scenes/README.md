# Visual scenes

This example contains four data-only music visualizers. It has no Wasm worker.

- Spectrum uses frequency bands.
- Waveform uses a bounded waveform.
- Spectrogram shows recent frequency bands.
- Particles use the media clock and audio levels.

Install the folder through the local development package option. Enable the package. Select a visualizer in the music player.

The repository address is an example. Change it before you publish this package in its own Git repository.

The host supplies colors from the current theme. Reduced motion stops particle motion and feedback. Hidden surfaces release the audio observer.

This package requests audio analysis only. The host rejects network or background-session permissions in a visualizer package.
