# Online subtitle search responsiveness

## Failure and cause

The Windows review build at `11adca2a52` could stop responding for several
minutes when the user selected **Online providers**. Repeated samples from
its UI thread contained package reads, manifest validation, canonical-path
checks and `AddonManager::providersForFeature`.

The display catalogue already used joined work. The search path still called
synchronous provider enumeration from its configuration check, language
check, provider selection and result filtering. Enumeration also inspected
packages unrelated to subtitles. Each returned candidate could cause another
scan.

## Change

The mounted search path now starts a cancellable request immediately. Provider
selection reads one fresh catalogue on joined work. It filters contributions
by feature before it checks keys and account configuration. The coordinator
keeps the request pending until selection finishes; its deadline includes
that preparation. Cancellation, timeout and invalidation discard late results.

The shared metadata service and the standalone subtitle coordinator both use
this path. Portable calls also prepare their release, settings, account,
grants, context, worker path, TLS support and approved keys on joined work.
Owner-thread process admission uses copied values and native tokens. A public
request ID remains stable across preparation and process execution.

Native subtitle availability uses a safe-mode snapshot bound to the Store
and Manager revision. Unknown, stale or active safe-mode state denies the
request. A display row can filter a result but cannot authorize a download.
The asset broker still checks the exact provider configuration on Save.

Ordinary portable results are published after clean process EOF. Duplicate
results, late host calls, incomplete output, process failure and forced exit
fail the call. Native-plan recipes still require their exact attempt admission;
ordinary requests cannot create native completion receipts.

## Work limits and idle behavior

- At most 64 preparations per Manager, including the running preparation.
- Joined preparation shares the process-wide limit of two repository jobs.
- At most eight prepared provider processes across the app.
- Existing recipe and secret byte limits remain in effect.
- An idle queue starts no process, thread or recurring timer. A process-slot
  wait uses native notifications. Invocation polling ends with the active call.

Cancellation and Manager retirement do not wait for a blocked file read on
the UI thread. Joined app shutdown retains the work until its captures end.
A slow file system can still delay the search result; it must not stop the
window from processing input.

## Evidence

WSL Linux with Qt 6.8.3 passes all 349 checks both normally and with
ASAN/UBSAN, with no failures, skips or runtime warnings:

| Suite | Cases |
| --- | --- |
| Prepared metadata invocation | 45 |
| Native authority | 16 |
| Metadata attempts | 74 |
| Metadata coordinator | 84 |
| Subtitle sources | 60 |
| Shared metadata service | 70 |

Linux link-wrap probes hold the real catalogue and provider-authority readers
before their first file read. Tests confirm that request entry and cancellation
return within 100 ms on the test host, and that the owner event loop continues
while a read remains blocked. Tests cover disable, owner destruction, timeout,
fallback order, safe mode, real Rust/Wasm execution and clean process EOF.
The subtitle test uses the shared service and the actual native subtitle adapter.

The normal proof has 87 fresh producer objects and pins 1,564 input files.
Its record is `/tmp/hakone-subtitle-online-proof/normal-proof.json`.
The sanitizer dependency closure has 38 fresh producer objects and pins
1,481 inputs. Its record is `/tmp/hakone-subtitle-online-proof/san-proof.json`.
An existing translation test used a GUI application cast with a core-only
application. Both event calls now use `QCoreApplication::instance()`; the
corrected suite passes normally and under sanitizers. The other 87 normal
producer object hashes are unchanged.

The [new Windows build](windows-subtitle-build-2026-10-29.md) is published in
the usual local folder. Restart the app before testing. Manual Windows GUI
confirmation and the full app performance comparison remain open.
