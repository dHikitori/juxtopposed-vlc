# Windows network-file read cancellation

The user confirmed that an audio-only network file kept playing after Close.
The app then exited after about one minute. No thread capture identifies that
wait. The earlier Stop-order fix is in the published local Windows build. This
read repair addresses another possible shutdown delay.

Desktop Windows filesystem and SMB access now use a native cancellation helper
for synchronous reads of regular remote files. The helper keeps the original
file handle, file position, and read operation. One native waiter handles
interrupt requests. The interrupt callback sets an event. The waiter retries
cancellation if the read has not yet started. The reader waits for the waiter's
acknowledgement before it can return or start another operation. Each read has
a nonwrapping sequence; an extra wake cannot process an old read again.

Close joins the waiter. Repeated Close also waits for that join. Positive bytes
and EOF survive a cancellation race. Interrupted errors return `EINTR`. Native
and CRT error state is preserved. The helper never closes the caller's file.
Its owner must retain it until all read and Close callers return.

The filesystem adapter covers direct UNC paths, extended UNC paths, and mapped
remote drives. Local files, devices, `fd://`, `stream://`, and Windows Store
keep their existing read path. Remote read requests use an `INT_MAX` bound;
larger requests return a valid short read.

This repair does not cover remote reparse targets, blocking Open, file or
directory queries, Seek, credentials, other access protocols, or a driver that
refuses cancellation. A refusing driver can still delay exit. The app retains
its buffer and waits for the real operation to end.

Microsoft describes the thread access, completion, and cancellation rules in
[CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio).
Its [cancellation guidance](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)
also describes the risk of cancelling a later operation and the driver limit.

## Verification

The exact applied source bytes pass 15 groups and 298 checks twice on actual
Windows. Tests use current Core interruption contexts, blocked native and CRT
named-pipe reads, a private regular file, and the real filesystem and UNC Read
functions. They cover pre-issue cancellation, error and count bounds, positive
bytes, EOF, Seek position, repeated Close, driver refusal, a delayed cancel that
crosses completion, an extra wake, and sequence exhaustion.

Three deliberate defects fail their expected assertions: one cancellation
attempt, no acknowledgement, and no sequence filter. Each control releases its
owned I/O and joins its threads before it reports failure. Mapped-drive path
classification uses test-only drive-type data. The test creates no mapped
drive and makes no SMB server connection. No user process, profile, file,
credential, or GUI action is used. No Windows sanitizer claim is made.

Windows Store preprocessing and object checks contain no helper declaration or
symbol. Linux preprocessing and syntax checks also pass without the helper.
Automake and Meson register the desktop module sources and fixture.

The final proof is `/tmp/hakone-win-file-interrupt/final-proof.json`, hash
`3a8f8f63169993a104d88d01e55be94505d5d6bcceeacd36d9539e228917a69d`.
The earlier `adbef0ed` packet is defective and superseded. All eleven applied
source and registration files match the final proof.

The full Windows app now builds from clean source `fd3fed4173`. All 868 managed
files and 416 PE checks pass. A fresh Windows console check imports all seven
add-ons and exercises the Manager snapshot factory. The package is published
in `build-windows/vlc-win64`; the extra staging copy has been removed after a
complete hash comparison. See [the build record](windows-local-build.md).

The unchanged fixture binaries also pass all 15 groups twice with this exact
published Core and LibVLC. All three race controls fail as expected. The proof
is `/tmp/hakone-win-network-file-current-core/final-proof.json`, hash
`7e5f8f71d3297073af7d9aa32aac96d07af7c0667ac16ae55c376753b551886a`.
The user's audio-only network-file Close retest remains required. No captured
stack establishes the cause of the original wait.
