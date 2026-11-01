# Accounts, picked files and temporary settings

These settings controls are owned by the app. The add-on declares them in its
settings schema; the user works with the app's own form and dialogs. The
add-on never sees a token, a sign-in code or a local path.

## Account sign-in (device code)

An account field is a secret string with an account recipe. The app supports
one recipe: the OAuth 2.0 device authorization grant (RFC 8628).

```json
"account": {
  "type": "string",
  "x-hakone-secret": true,
  "x-hakone-control": "account",
  "x-hakone-label": "settings.account",
  "x-hakone-account": {
    "recipe": "oauth-device",
    "deviceAuthorizationUrl": "https://auth.example.org/device",
    "tokenUrl": "https://auth.example.org/token",
    "clientId": "my-public-client",
    "scopes": ["read"]
  }
}
```

Rules:

- Both endpoints are exact HTTPS URLs without user data. Their origins must be
  in the package's `network.http` origins, so the install review shows them.
- `clientId` is a public client ID (1 to 256 visible ASCII characters). Do not
  put a client secret in a package.
- `scopes` is optional: at most 16 distinct scope tokens.
- Bind the field with `credentials.use` (usually `Authorization` with the
  `Bearer ` prefix). The app injects the access token only for that origin
  and header.

The flow:

1. The user selects **Connect account…**. The app asks the device
   authorization endpoint for a code.
2. The app shows the code and the sign-in site's host name. **Open the
   sign-in page** opens the site in the system browser; **Copy the code** puts
   the code on the clipboard.
3. The app polls the token endpoint at the interval the service gives (5 s
   when it gives none; 5 s more after `slow_down`). The code expires after the
   service's `expires_in`, at most 30 minutes. Polling runs only while the
   sign-in dialog is open.
4. A `200` reply with a bearer `access_token` connects the account. The app
   keeps the access token, and the refresh token when there is one, in the
   secure credential store. When that store is not available, the user can
   keep the account for this session only.

The app shows its own messages. It never shows `error_description` or other
text from the service. An add-on cannot type or replace an account token
through settings; only the sign-in sets it.

**Refresh.** When a provider returns `auth_required`, or a worker starts after
the access token's `expires_in`, the app exchanges the refresh token once
(at most once a minute for each account). The new tokens replace the old ones
under the same credential references, so the account identity, cached
answers and queued items stay valid. Nothing runs while the add-on is idle.

**Disconnect.** **Disconnect** then **Save** removes the access and refresh
tokens from every saved settings generation.

## Picked files and folders

A string field with `"x-hakone-control": "file"` or `"folder"` shows the app's
own file dialog. The setting's value is an opaque handle such as
`handle:3f2a…`; the app keeps the path and shows the file or folder name. A
pick is a draft until **Save**. Clearing the field removes the handle.

The handle is not a path. No host operation reads a file through a handle.

## Temporary settings

**Use until the app closes** applies changed ordinary settings over the saved
ones, for this session only. They are not saved. Secrets, accounts and picked
files cannot be temporary. Running workers stop, so the next call uses the
temporary values; cached answers from the saved settings are not reused.
**Stop temporary settings** returns to the saved values.

The order is: schema defaults, saved settings, temporary settings.
