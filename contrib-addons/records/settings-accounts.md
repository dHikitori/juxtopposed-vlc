# Account sign-in, picked paths and session settings (Step 6.4)

Step 6, row 4 of the [continuation plan](../07-continuation-plan.md).
Written 31 October 2026. Implements part of
[UI, themes and configuration](../04-ui-themes-and-configuration.md)
(generated configuration, accounts and secrets). Author guide:
[SDK](../sdk/settings-accounts.md).

## Before

File, folder and account controls emitted QML signals that nothing handled.
There was no account recipe, no token refresh and one layer of settings.

## Device-code account recipe

- **Declaration.** A secret string field with `x-hakone-control: "account"`
  and `x-hakone-account` (`recipe: "oauth-device"`, HTTPS device and token
  endpoints, public `clientId`, optional `scopes`). `DeviceRecipe::from`
  (`addons/addon_device_authorization.*`) checks it at manifest load. Both
  endpoint origins must be declared `network.http` origins.
- **Flow.** `DeviceAuthorization` runs RFC 8628 with an injected transport.
  The production transport (`devicePost`) uses no redirects or cookies, a
  15 s limit and a 64 KiB reply limit. It handles `authorization_pending`,
  `slow_down` (+5 s), `access_denied` and `expired_token`. A code lasts at
  most 30 minutes. The app shows its own text, never server text. Tokens
  must be bearer tokens of visible ASCII.
- **View.** `AddonAccountSetup` (QML type in `VLC.Dialogs`) and
  `RedesignAccountSetupDialog.qml` show the code and the sign-in host, open
  the page in the system browser and copy the code. Polling runs only while
  the dialog is open. The form asks the user to save or cancel edits first.
- **Storage.** `AddonManager::connectAccount` stores the access token as the
  field's credential and the refresh token as `<field>.refresh`, with
  `_accounts` (a list of `{field, expiresAt}`) in the same settings
  transaction. The store accepts the `.refresh` reference only for an
  account field. Session-only storage is offered when the secure store is
  not available. Settings cannot replace an account token by typing.
- **Refresh.** On a provider `auth_required`, or at worker start after
  `expiresAt`, `refreshAccounts` exchanges the refresh token at most once a
  minute for each account. `CredentialStore::replace` keeps the same
  references, so the account reference, cache partitions and queued items
  stay valid. No timer runs while idle.
- **Disconnect.** The ordinary delete operation removes both references
  from every settings generation.

## Picked files and folders

`AddonManager::pickSettingsPath` calls the app's picker hook. `mainui.cpp`
installs a non-blocking `QFileDialog`. `registerPickedPath` returns an
opaque `handle:<id>` and the display name. Save accepts only a handle saved
for that field, or one picked for it in this session. The path is kept in
`_handles` in the settings record, and settings values carry only the
handle. The add-on has no host operation for handles yet, so none is
advertised.

## Session settings

`setSessionSettings` keeps changed ordinary values in memory over the saved
ones (no secrets, accounts or picked paths). Workers restart and get
`effectiveSettings`. The cache partition gets a session suffix, so answers
from the saved settings are not reused. The form has **Use until the app
closes** and **Stop temporary settings**.

## Tests

| Test | Label | Result |
| --- | --- | --- |
| `test_qt_addon_accounts` (new) | profile | 25 pass: 8 recipe rows, the device flow with a pending poll, slow-down, 7 refusal and bad-reply rows with no server text, cancel, bearer token checks, manifest refusal, manager connect, refresh with the same account reference and once-a-minute limit, disconnect, picked folder handles, session overlay and partition |
| `tst_AddonUi` | (qml) | 4 new: account connect and disconnect without typing, edits before connect, picked folder name and handle, temporary settings |

All labels pass: profile 29, metadata 53, source 22, ui 7, app 6, session 4,
visualizer 12.

Not done: profile and account-scoped value layers (the settings record has
one account per field, and token replies name no account), authorization
code with PKCE, a guest operation that reads a picked file or folder, and a
refresh seen by other app windows that cached the old token. Not run on
Windows or against a live sign-in service.
