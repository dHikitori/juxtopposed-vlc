# Host session panel

This layer has no app factory or room service. It does not mount Manager host
operations or create a native player adapter. Root owns app assembly.

`AddonSessionUi` is a trusted host QML facade. Do not pass it to guest UI
commands. Create it with the existing `AddonSessionHost` and `AddonManager` on
their Qt owner thread. An injected backend supports focused tests. A separate
Host-plus-catalog constructor supports actual Host approval tests without a
Manager or native player constructor.

The read-only properties are `controllers`, `sessions`, `review`, `preparing`,
`starting`, `errorMessage`, `status`, and `revision`. The actions are `prepare`,
`accept`, `cancelReview`, `viewClosed`, `resume`, and `end`. `refresh` updates the
catalog and active session list. C++ `close` cancels an unapproved review and
closes the facade. It does not end an approved background session.

The Manager adapter lists enabled, configured `session@1` controllers with a
declared `run` operation. It requires the available worker and WSS handlers.
The list projects granted relay origins and playback operations. It includes
no credential, account reference, or package path. Host approval still performs
the authoritative declared/granted permission and account checks.

The panel uses `RedesignTheme`, `RedesignButton`, and `RedesignComboBox`. Its
text inputs use the same field, focus, radius, and text tokens. Add-on names,
room names, relay addresses, status, and errors use plain text. Relay input is
limited to 2,048 characters. Room input is limited to 256 characters.

`prepare` creates no session. The ticket stays private in C++. The review shows
the captured add-on name, canonical relay origin, room, observation fields,
background behavior, and permitted control choices. Every control choice is off
by default. `accept` with no operations selects observation only. A requested
subset cannot add an operation, use an unknown operation, or repeat one.

Before acceptance, the facade reads the ticket again and requires the same
captured fields. A stale or expired review is refused. The native media stamp
check remains in Host activation. Acceptance can therefore enter a pending
start and then end if playback changed. The panel must not report an active
session until Host confirms it.

Closing or hiding the panel calls `viewClosed`. This cancels a pending review.
It does not end a session that the user already approved. The review states
that background behavior before acceptance. Suspended sessions have a separate
host Resume action. End remains a host action outside guest controls.

Manager revision changes refresh the catalog. Pending review expiry polling
reads only the retained Host ticket; it does not poll Manager files. Host or
catalog owner loss closes the facade. Generation checks discard a ticket that
returns after the view canceled its preparation. Inline readiness and refusal
events cannot create automatic approval. The destructor publishes no new UI
change.

The isolated recipe is `modules/gui/qt/tests/session-ui/CMakeLists.txt`. It
builds the actual committed Host closure with strict Qt definitions and no
native player creation. The Manager adapter has a separate object compile.
Normal app linking and factory checks remain the app assembly gate. The QML
suite uses the existing `qml_test` runner. Exact counts and source hashes are in
`/tmp/hakone-session-ui-validation.json`.

The facade passes 18 normal and 18 sanitizer cases. The QML panel passes eight
cases. The sanitizer run enables address, undefined-behavior, floating-cast,
leak, and stack-use-after-return checks. Both sanitizer and QML diagnostic
files are empty. The tests include actual Host approval with a bounded fake
player backend. They create no native player or Manager service mount. The
separate Manager adapter object compiles with the strict Qt definitions; its
full app link remains part of assembly.

Every review call retains a copied callback and the exact ticket generation.
After the callback, the facade checks owners, closure, ticket, and generation
before publication or acceptance. The tests delete the facade inside a review
callback and replace a review during readiness and acceptance. The old callback
cannot publish or approve the replacement. Manager capability changes also
refresh the controller list.
