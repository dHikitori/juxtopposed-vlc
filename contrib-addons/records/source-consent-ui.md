# Host source playback review

`AddonSourceConsentUi` owns one review for the trusted host UI. It keeps the
native review ID private. The backend can prepare, read, approve, revoke, or
discard a review. A package view cannot invoke these host choices.

Preparation grants nothing. The UI reads the captured source, account, and
public HTTPS profile. It refuses unknown authority fields or wider profiles.
Before approval or removal, it reads the same ticket again and requires the
exact displayed map. The ticket has one use and a 30-second steady-clock limit.
The native owner still performs the authoritative current-state check.

The facade copies callbacks and signal arguments before it calls external
code. It checks owner lifetime, ticket generation, and pending action state
after each review read. A callback can delete the UI, replace a review, or
start the one pending change. An old callback cannot replace the new review
or start the same save again. A closed view discards its review. It does not
remove saved permission or reopen on a late save result.

The shared-theme dialog displays source and account text as plain text. It
offers separate Allow and Remove actions. The discovery browser has a host
Playback access button. Its parent supplies the optional consent backend.
Without that backend the button stays hidden. The popup opens a review; it
does not approve one.

All 20 facade cases pass normally and with address, undefined behavior,
floating-cast, leak, and stack lifetime checks. The dialog passes nine QML
cases. The discovery suite passes 12 cases, including the explicit parent
review action. Both QML diagnostic streams and the sanitizer stream are empty.

The isolated recipe compiles the actual facade and tests with strict Qt
definitions. Exact source, executable, and log hashes are in
`/tmp/hakone-source-consent-ui-validation.json`.

## Native app binding

The native constructor binds this panel to `AddonDiscoverySourceOwner` through
guarded owner pointers. The real binding passes nine normal and nine focused
sanitizer cases. These cases include saved Allow and Remove actions, closing a
review without approval, failed capture, Manager retirement, and both object
destruction orders. Both diagnostic streams are empty. Exact hashes are in
`/tmp/hakone-source-consent-native-binding/final-proof.json`.

MainUI creates the owner with the shared admissions store and app work handle.
It binds the panel as `AddonSourcePlayback` and supplies it to the discovery
window. The source factory puts stable references in the native queue. Only a
native input open can ask the provider for a stream address.

The C++ app reports `mediaStreamAvailable` after the native owner becomes
active. The default is false. Repeated state reports do not send duplicate
notifications. Retirement reports false and refuses a later true report. The
setter is not a QML method. All 39 runtime cases pass normally and with focused
sanitizers, including the actual packaged worker. There are no skips or
diagnostics. See `/tmp/hakone-native-stream-runtime-validation.json`.

App retirement closes the source owner and panel before app drain. Manager
retirement closes the native source route before worker shutdown waits. The
actual MainUI object compiles with the configured strict app flags. Full app
linking, compiled QML, and supported-platform app checks remain open.
