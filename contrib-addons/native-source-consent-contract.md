# Native source consent

`AddonSourceConsent` is a host service. It has no worker operation or QML write API.
The user approves source playback when they accept a package's `media.stream`
permission: in the install review, at Enable, or in an update prompt that adds
it. Bundled packages carry that grant. The host then saves the approval with
`approveAccepted()` the first time it captures the enabled source, or at its
first native playback open. There is no separate approval step in the app.
Turning the add-on off withdraws playback at once. A changed repository or
account is a new binding; the host saves it only while the package is enabled
with the grant.

## Saved approval

The host captures a ticket before it shows an approval dialog. The ticket binds:

- The canonical repository or app distribution identity and source kind.
- The contribution identifier and host account reference.
- The `public-https-media@1` transport profile.
- The saved approval epoch and revision.

`approveAccepted()` captures and grants a ticket in one call. Only host C++
code can grant the captured ticket. Each ticket has one use and an
expiry time. A changed source, account, saved revision, or local reset rejects it.
The host must show the captured source, account, and profile in the dialog.

The store uses `source-consent/approvals.json` under the host data directory.
It does not use add-on settings or guest storage. A saved approval has a random
identifier. Revocation and a later grant cannot revive an old authorization.
Code-only releases retain source identity. A release still needs its own captured
package and configuration check before each operation.

## Native playback intent

Only a native primary playback open can mint an intent. Queue insertion, import,
preparse, thumbnails, and background work cannot mint one. The native caller must
supply the exact admitted `hakone-addon-source:v1:` URI, a positive input generation,
and the native playback reason. The current native request proves only
`PrimaryPlayback`. It does not prove a new UI click or distinguish queue advance
from replay. A future native caller can use `QueueAdvance` or `Replay` only if it
has that information.

Each intent has one use and a short expiry time. A wrong URI, generation, source,
account, or reason consumes the token and rejects it. A bounded generation window
prevents token recreation for an old input. Stop does not create a replay intent.
A replay needs a fresh native generation and a new intent.

After consumption, the immutable authorization can remain active for the input
lifetime. `current()` checks the saved approval, source, account, media grant, and
admission again. The resolver must also check its captured package configuration
and native job before each outbound request and before result delivery. A failed
check must revoke the input transport without a GUI event.

## Bounds and lifetime

The default limits are 256 saved approvals, 64 tickets, and 128 pending intents.
The saved file cannot exceed 1 MiB. Tickets expire after 120 seconds. Intents
expire after 10 seconds. All limits have fixed host maximums.

The class has no `QObject`. Its checks use plain thread-safe captured state.
It invokes those checks outside its mutex. Reset and close invalidate local
authorizations at once. A failed save leaves local revocation in effect.
The optional test clock runs under the mutex. It must not call the store again.

The focused suite has 12 passing cases. It covers source and account changes,
tamper rejection, reset, expiry, reuse, bounds, write conflicts, and concurrent
native calls. This service does not enable stream playback by itself.
