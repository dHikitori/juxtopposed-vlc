# Extensions settings

The Extensions page uses a compact installed list. Each row shows its name,
version, state, and enable control. Search filters this list. Select a row for
settings and update actions. The More menu contains version, removal, and
recovery actions.

The Add extension action opens repository and release inputs. A local package
is an explicit development option. The app shows permissions before installation.
Installation and enablement remain separate decisions. A closed review keeps its
prepared candidate. A new candidate resets the enable decision. Capability
changes refresh the review without an automatic open.

The page creates settings, install, and recovery dialogs only when requested.
The app supplies an asynchronous package summary. The page requests this summary
on activation. Recovery uses the same rows. Search and selection do not read
package files. A fixture without this service can use one snapshot per revision. Names, source data,
errors, and permission details use plain text.

The Advanced section contains the legacy Lua startup option. This option does
not control enabled Hakone add-ons. The app keeps all prior install, update,
rollback, removal, saved-data, and recovery actions. A destructive action still
needs confirmation and a current package revision.

## Reference and validation

The layout follows `settings/extensions-settings.svg` and the settings transcript
at 15:35–19:50 under `.design-reference/juxtopposed/`. The reference places the
installed list before search and actions. Advanced controls use a disclosure.

The offscreen fixture uses the production QML controls in the same settings
frame as the General fixture. It covers dark and light themes, a narrow frame,
keyboard focus, setup details, and install review. This fixture does not execute
the native Preferences window or the Windows app.

The first layout proof and image hashes are in
`/tmp/hakone-extensions-ui/final-proof.json`. The asynchronous UI hook proof is
in `/tmp/hakone-extensions-ui/async-proof.json`. The existing Installer, Recovery,
and Preferences suites cover permission approval, explicit enablement, update
review, stale confirmations, dialog lifecycle, keyboard focus, and backend reads.

## Stall investigation

The old page created install, settings, and recovery content on the first visit.
Management and Recovery each read the native package list. Each native package
read also validates files and reads settings. A Windows fixture measured one
seven-package read at 421 ms on a local temporary store and 1598 ms on a WSL
UNC store. Repeated synchronous reads can cause a visible interface stall.
These measurements do not prove a deadlock.

The asynchronous UI hook shows distinct load, error, and empty states. An error
has a retry action. The page does not call the synchronous package getter when
a summary service exists. Row actions require a current summary at the request
and final confirmation. Recovery has the same guard. A refresh closes an open
settings form. Add, prepared-candidate review, request cancel, and the global
pause control remain available.

Cached rows cannot authorize a package action. The native action handler remains
the authority. Native summary lifecycle tests and a fresh Windows runtime test
remain separate validation requirements. The QML fixture does not prove them.
