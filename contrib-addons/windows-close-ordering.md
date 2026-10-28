# Playback stop before user-quit cleanup

The user reported that a responsive Windows app kept playing an audio-only network file
after Close, then exited after about one minute. The process had exited before
inspection. No thread capture identifies the operation that caused that wait.

The source has a clear ordering problem. A real window close hides the window.
The video path requests Stop, but the audio-only path proceeds to quit without
that request. Quit then waits for joined add-on work before it calls Core quit.
Core also closes interfaces before it deletes the main playlist. Audio can
therefore continue during both waits.

The real quit path now closes new add-on admission and requests playlist Stop
before it starts the joined cleanup wait. OS user-quit and recovered event-loop
exit use an early, once-only stop hook. Native interface retirement keeps its
existing behavior. Explicit close-to-tray still follows its existing path.

Stop and final app destruction remain separate. The app still waits for counted
workers and their cleanup before it releases Qt. The fix does not force exit,
abandon a thread, or treat a timeout as proof that work has stopped. Callback
reentry and destruction cannot pass the early-stop callback's tail. Ready
callbacks complete before queued native cleanup.

## Verification and remaining evidence

The exact applied coordinator and test sources pass 22 normal and 22 focused
AddressSanitizer and UndefinedBehaviorSanitizer cases, with leak and stack checks.
There are no failures, skips, or runtime diagnostics. The cases cover a blocked
joined worker, an early stop request, a responsive event loop, deferred Core
quit, repeated requests, native retirement, callback reentry, deletion, and four
actual QCoreApplication exit paths. They test callback ordering; they do not
claim playback of the user's network file.

The complete `a51d1c9fc3` Windows app now compiles and links the DialogsProvider
and Qt startup call sites. All 416 PE files pass the dependency check. The local
build has been published to `build-windows/vlc-win64`. The focused proof is in
`/tmp/hakone-shutdown-ordering/final-proof.json`.
The source audit and parity with the earlier Windows build are in
`/tmp/hakone-windows-close-audit`.

The specific one-minute blocker remains open. The normal drain retries every
five seconds; it has no fixed one-minute deadline. A file or credential read
already in progress can delay joined cleanup. That is a possible delay path,
not a captured cause in this report.

A local diagnostic recorder passes its minidump self-test against an owned
helper process. Its five-minute recording window saw no matching VLC process
and collected no user-app dump. A later repeat needs a fresh capture. Recorder
files are in `/tmp/hakone-close-capture`; no user process was stopped by it.
A second five-minute window after publication also saw no matching process.
The actual Close test with the user's network file remains open.

The user later confirmed audio-only playback. This fits the missing early Stop
path; it does not identify the operation behind the one-minute wait. A separate
[Windows remote-file read repair](windows-network-file-close.md) now passes its
actual Windows cancellation checks. It still needs the full app rebuild and
the user's Close retest.
