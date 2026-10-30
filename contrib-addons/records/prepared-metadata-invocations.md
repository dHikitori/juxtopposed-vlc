# Prepared metadata invocations

The host can now prepare a portable metadata call on joined work. The first
adapter takes a selected slot from `ArtworkAttemptPlan`. Ordinary interactive
metadata calls now use joined preparation too; see [the subtitle search
fix](subtitle-online-responsiveness.md). Native artwork owner activation and
the serial plan runner remain open.

## Preparation and admission

Create the preparer on the Manager thread. Run it on joined work. It checks
the exact release, contribution, settings revision, account, grants and
projected context. It reads saved files, checks the worker and TLS backend,
and reads approved key bytes on that work thread. The returned object holds
copied values and native authority tokens. It holds no saved-state reader.
Discard the preparer before final native admission.

`invokePreparedMetadata` consumes that object on the Manager thread. This
call uses memory only. A separate storage context supplies its captured
configuration to private storage calls. Explicit final saved-state admission
remains the runner's duty.

The runtime bounds retained data and work:

| Resource | Limit |
| --- | --- |
| Prepared recipes | 64 per process |
| Active child processes | 8 per process |
| One key field | 16 KiB |
| Key bytes in one recipe | 1 MiB |
| Key bytes across recipes | 16 MiB |

Creating the prepared runtime starts no process or timer. Each active call
gets a joined retirement obligation before its child starts. Its result can
transfer a receipt only after clean process EOF. Extra frames, incomplete
output, forced exit, cancellation and stale authority prevent certification.

Credential preparation uses the same HTTPS origin rules as the manifest and
broker. An explicit default port or root slash can name the same approved
origin. A declared binding still needs an exact approval. A guest receives
key-presence flags and selectors; the broker supplies approved header bytes.

## Shutdown

QObject weak pointers can remain valid while a Manager member is destroyed.
The runtime therefore has a separate closed-owner flag. Cleanup checks that
flag after it deletes the child worker, before it accesses Manager maps.
The signal callback and queued cleanup share one capture. The native join
ends after that capture releases its Qt values.

Cancellation and retirement wipe active recipe keys before native
cancellation. The final cleanup releases the process permit after the
worker and callback values end.

## Evidence at `15f7568633`

Current source passes 120 normal and 120 ASAN/UBSAN checks on WSL Linux with
Qt 6.8.3. There are no failed cases, skipped cases or runtime warnings.

| Suite | Normal | ASAN/UBSAN |
| --- | --- | --- |
| Prepared invocation | 30 | 30 |
| Native authority | 16 | 16 |
| Metadata attempt | 74 | 74 |

These builds use a fresh 30-CPP dependency closure and its aggregate MOC
object. They exercise the actual Rust worker and French TV Wasm module,
approved header delivery through the broker, process EOF, quotas,
cancellation, owner removal and the final callback capture.

The teardown regression runs queued cleanup from a NetworkBroker member's
destruction signal while the Manager weak pointer is still valid. A separate
control removes only the closed-owner check. ASAN then reports a heap
use-after-free in the Manager's invocation-map cleanup. The fixed code passes
the same case.

All three build definitions register the source and regression. The actual
standalone CMake configuration succeeds, including the Linux wrapper and
MOC definition. The configured Meson runtime parses the updated build file.
Full app linking and supported-platform conformance remain open.

The proof is `/tmp/hakone-prepared-metadata-current/final-proof.json`, SHA-256
`d3ab0cb304c42e78bde427b9540c3289ddea7d71322c7e0a9c9ce27b3276c443`.
Completed binary and object outputs are retained as verified compressed
archives. Their raw copies are removed after their consumers end.

The initial Windows checkpoint was `11adca2a52`. The current local package
at `335ac989d0` includes prepared invocations and asynchronous provider
selection. See [its build record](windows-subtitle-build-2026-10-29.md).
