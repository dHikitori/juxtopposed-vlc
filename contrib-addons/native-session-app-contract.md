# Session app owners

`MainUI` creates one `AddonPlayerSessions` owner with the app Manager, the
native player, the player controller lifetime, and the shared app work handle.
The owner creates the native player adapter, session Host, controller loop, and
trusted UI facade on the app thread. `activate()` registers the complete host
operation set after construction. It grants no session or playback control.

The controller invokes the selected add-on through `AddonManager::invoke`.
Manager operation results return to that controller. Package withdrawal ends
the matching Host sessions. The host keeps native authority and invalidation
watches inside the existing session boundary.

The app registers the facade as the `AddonSessions` singleton. The Extensions
settings page loads `RedesignAddonSessions`. It uses the shared theme and host
controls. A hidden or closed panel cancels its pending review. An approved
background session continues until the user selects End, its authority ends,
or the app closes.

Manager retirement closes the assembly before its service members are
destroyed. App retirement also closes it through `MainCtx::addonWorkRetiring`.
Player controller destruction closes it before the native player can be
released. The `MainUI` destructor closes direct session owners before other
child teardown.

Close stops the controller, closes Host sessions and registrations, and then
closes the native adapter. The UI does not publish intermediate retirement
states. A guarded signal hold ends before the facade publishes its final close
state. The assembly destructor suppresses this final UI signal too. Close is
safe to repeat. A failed or conflicting host registration rolls back only its own
partial registration. A closed app cannot activate it.

The actual-player assembly suite passes all 10 cases normally and with focused
address, undefined behavior, floating-cast, leak, and stack lifetime checks.
The eight behavior cases cover registration and removal, native revocation
before UI publication, player lifetime loss, destructor publication, Manager
retirement, closed app refusal, conflicting registrations, and owner deletion
during registration. The other two cases are test setup and cleanup.

The focused run uses `VLC_TEST_PLUGIN_PATH` with only the fixture's `adummy`
and `araw` modules. The test header sets the native plugin paths from that
variable. Setting only `VLC_PLUGIN_PATH` does not limit this fixture. Both
earlier full-scan runs passed the 10 behavior/setup/cleanup cases but reported
the known startup baseline of 167,344 leaked bytes in 1,160 allocations. Those
results remain separate. The focused run uses no leak suppression.

The isolated assembly recipe is
`modules/gui/qt/tests/player-sessions/CMakeLists.txt`. It compiles the actual
assembly, UI facade, Manager adapter, native player adapter, and real native
fixture. The existing normal host archive and Core libraries supply the other
components. The actual MainUI object and all five new app implementation and
metaobject objects also compile. Exact source and artifact hashes, test logs,
build results, and the two full-scan results are recorded in
`/tmp/hakone-player-sessions-validation.json`.

The complete application link, compiled QML run, two-player
correction checks, and supported-platform execution remain release gates.
