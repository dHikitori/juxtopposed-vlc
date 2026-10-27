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
`/tmp/hakone-source-consent-ui-validation.json`. The native-owner bridge,
production app mount, native owner/queue tests, and fresh full app run remain
separate integration gates.
