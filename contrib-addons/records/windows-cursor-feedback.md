# Windows cursor report

The user reports an arrow with a spinner for the full VLC session. The cursor
also appears outside the VLC window. It returns to normal when VLC fully
closes. This report remains open.

The cursor is not a CPU usage meter. High CPU use can delay input and drawing.
Windows also has a process startup feedback cursor. Its startup flags control
that feedback. See the
[Microsoft startup information reference](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow).

## Current evidence

- The package checked for the initial report was source `335ac989d0`. Its player has
  PE subsystem 2, GUI. Its add-on worker has subsystem 3, console.
- The source search found no add-on call that sets a busy or system cursor.
  Existing Qt view loaders use item-scoped busy cursors. Existing global Qt
  overrides serve resize and drag operations.
- The Qt 6.8.3 Windows process source has no explicit feedback flag. See
  [the Qt source](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/io/qprocess_win.cpp).
- The process query during this report found no running VLC or add-on worker.
  Thus no current CPU, launch-rate, or UI-thread sample proves the cause.

The worker is not yet established as the cause. An explicit background launch
flag can prevent startup feedback for a helper, but that alone cannot prove
that it fixes a cursor present for a full session.

The Microsoft reference describes startup feedback as a short interval, with
limited extensions during initial GUI work. One process launch does not
explain feedback for a full session. Repeated process launches and an uncleared
cursor state remain possible causes, without current evidence for either.
High CPU load can delay input; it does not by itself identify this cursor's
cause.

The refreshed local review package is source `74ae4267fc`. Its package and
worker checks pass. A new process query found no running player or worker,
so no live CPU delta or launch-rate sample was available. The
[build record](windows-artwork-build-2026-10-30.md) retains the exact package
hashes. No cursor fix is claimed.

## Next live check

Identify the exact running package. While the reported cursor is present,
record player and helper CPU deltas, helper start/exit counts, and UI-thread
samples. Check whether a resize or drag cursor override remains active. Then
repeat with the preserved build from before add-ons, with a separate profile.
Do not replace the user's profile or the preserved build.

The portable image background-work tests address UI blocking. They do not
close this Windows cursor report.


The 2026-10-30 follow-up process query also found no player or add-on worker.
Thus it provides no live CPU delta while the reported cursor is present.
The record is `/tmp/hakone-profile-writer-scope/cursor-live-query.json`.
The new writer component checks do not change or close this report.
