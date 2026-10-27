# Native source lease retirement

`vlc_source_lease_Cancel()` waits for the one transport cancellation callback
to finish. The canceled flag becomes visible before that callback starts.
The callback runs outside the lease mutex. A concurrent caller waits for
completion. A nested call on the callback thread returns without waiting for
itself. This exception permits the transport to cancel its own source job.

The last release keeps one live reference while cancellation runs. A callback
can retain that reference for later cleanup. The lease never resets a zero
reference count. Thread cancellation stays disabled through native cancellation
and final reference cleanup.

These rules let Stop, source withdrawal, and application retirement rely on
completed transport cancellation when their native calls return. A native
callback must still close its sockets and wake reads before it returns. It
must not wait for a GUI event.

The actual source fixture passes all 14 groups normally and with address,
undefined behavior, leak, and stack lifetime checks. It retains the 11 previous
groups and 200 existing result/cancel races. The three new groups cover a
blocked callback with a second cancel caller, with and without a retained
caller reference, and a final-release callback that retains a cleanup
reference. The previous once-flag implementation fails the second-caller
completion assertion.

The proof compiles the actual lease implementation and fixture against the
current normal Core thread and policy helpers. Its source hashes, executables,
recipe, and logs are recorded in `/tmp/hakone-source-lease-validation.json`.
The normal Core rebuild now passes all 54 build actions. Its registered source
fixture passes all 14 groups against that fresh library. The configured source
and fixture resolve to the current worktree files, and both source hashes
match. The full application still needs a fresh build after this change.
