# Native library targets

`AddonNativeLibraryStore` connects discovery selection to the native playlist
transaction API. This adapter does not resolve a source reference to a stream
URL. The saved media MRL is the exact admitted `vlc-addon-source:v1:` reference.

## Host selection

The core picker lists at most 200 playlists on the media-library thread. It
captures a flat database snapshot for each choice, then reads its current name.
It gives the UI only a name and an opaque choice token. Search accepts at most
256 characters. A result with more playlists asks the user to refine the search.
Read-only playlists do not enter the choice list.

Selection consumes the choice token. The picker passes that exact snapshot to
`adoptSnapshot` and receives an opaque `library:<UUID>` target. It never creates
a replacement snapshot when the user selects an old choice. A deletion, rename,
or ID reuse can make the snapshot stale. The native transaction then rejects it.

The C++ host can also call `selectPlaylist(playlistId)` directly. This method
queues snapshot creation and reports completion through `targetReady`.
No playlist ID, media ID, database snapshot, or file path enters a guest descriptor.

The adapter keeps at most 16 targets and four pending native jobs. Canceled jobs
keep their slot until the task exits. The picker must call `releaseTarget` when
it discards a selection. Each target permits one append attempt. A new attempt
needs a new host selection. The adapter never binds an old target to a replacement
playlist after deletion or ID reuse.

The discovery backend reads only the cached availability, opaque identity, and
local revision. The native database snapshot supplies the commit authority.
Qt event counts do not replace the database token.

## Dispatch and lifetime

The dispatcher has these exact function types:

```cpp
quint64 run(const QObject* receiver,
            std::function<void(vlc_medialibrary_t*)> nativeWork,
            std::function<void()> guiCompletion);
void cancel(const QObject* receiver, quint64 task);
```

Call the dispatcher from the owner thread. It keeps the library valid while
native work executes. It calls completion on the owner thread. It can discard
completion when the receiver ends. `dispatcherFor(MediaLib*)` uses
`runOnMLThread` and `cancelMLTask` with the `addon-native-library` queue.

Native work uses captured data and thread-safe package-store reads. It does not
call the manager, a QPointer, or a GUI object. Close sets the shared cancellation
state and cancels queued dispatch. It does not wait for a UI event or a worker.
The flat native snapshot can be freed after the library or module ends.
The host must close this adapter before it destroys the manager or media library.
QObject destruction signals also close the adapter as a final guard.
MainUI closes the picker and library store before it destroys its child manager.
The compositor destroys MainUI before MainCtx destroys MediaLib.

The discovery owner retains a selected target for one source view. It releases
the target on close, cancellation, target reselection, and source invalidation.
An enqueue request can use only the target selected in that same view. The UI
must show an explicit Add action after target preparation. The provider receives
no playlist list, choice token, selected target, or native identifier.

## Admission and commit

The host validates each source, account reference, and required permission on
the manager thread. It captures the installed release, grants, settings, and
settings revision. Account and manager changes also cancel pending epochs.

The native task can prepare at most 200 external media records. Before commit,
it acquires the same `store.lock` as PackageStore. It then compares every captured
package configuration with current saved state. This check also detects external
package, settings, enable, and safe-mode changes without a Qt event.

The lock order is package store, then native database. The task holds the package
lock through one `vlc_addon_playlist_append_if_unchanged` call. It invokes no
adapter completion callback under either lock. The native database transaction
rejects a stale, replaced, missing, or read-only target. The complete group
commits in order, or the playlist and its counters stay unchanged.

Admission to that native call is the cancellation boundary. Cancellation before
admission prevents the append. Cancellation after admission cannot reverse a
completed commit. A caller can suppress result delivery after cancellation. It
must not report that suppression as proof of rollback.

Preparation creates a new external media record and its first catalog title in
one database transaction. The host removes control characters and line separators
from that title. An empty title uses the host text `Media item`. Existing records
retain their title, including a manual title. Notification occurs after commit.
Failure in title storage also rolls back the new media and file records.

Preparation can leave a reusable external media record after a later append
rejection or cancellation. That record contains its stable admitted MRL and first
display title. No stream or network request starts. A later attempt can reuse it.
Source admissions keep their existing count and byte limits.

The UI hides playlist selection if the native CAS API is absent. The host routes
`library:` targets to the library backend and `queue` to the queue backend.
Unknown targets have no fallback. Library storage does not start playback. The
existing playback and queue capability gates still apply to those actions.
