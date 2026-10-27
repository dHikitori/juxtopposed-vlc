# UI extensions, themes and configuration

## Public components, private implementation

Expose a versioned `hakone.ui` vocabulary of host-rendered components. The author supplies a declarative view document, typed state and registered command IDs. The application maps these to its own redesign controls, theme tokens, accessibility and layout behavior. The author does not import internal QML files or get the application's context objects.

This also solves the theme requirement: an extension's primary button and media card follow the effective theme automatically. Existing `RedesignButton.qml` and related controls are useful implementations, but their filenames/properties are not the SDK.

Do not load repository-supplied QML, JavaScript strings, HTML scripts or Qt plugins into the main UI engine. Qt's guidance explains that [QML has no separate privacy domains](https://doc.qt.io/qt-6/untrusteddata.html). The proposed renderer accepts data through an allowlisted schema and constructs trusted host components. It must never pass untrusted component names or bindings to dynamic QML compilation.

## A bounded view language

V1 vocabulary: Page, Section, Row, Column, Heading, Text, Button, IconButton, List, MediaCard, Table, Tabs, FormField, Image, Notice, Progress and EmptyState. Prefer high-level media and form components so authors do not recreate selection, focus and loading states. List/Table use host virtualization, stable keys and typed item models.

Properties use semantic values: text role, emphasis, primary/secondary/destructive intent, approved icon ID, size class and spacing token. Bindings are validated property paths into a declared state projection, optionally using a small allowlist of formatting helpers. There are no script expressions, arbitrary URLs, parent traversal, property reflection or binding to internal core state. Complex logic runs in the worker and emits bounded typed state patches.

Example static view for a discovery source:

```json
{
  "contract": "ui@1",
  "id": "radio-browser",
  "root": {
    "type": "Page",
    "title": {"message": "radio.title"},
    "children": [
      {
        "type": "Notice",
        "visible": {"bind": "state.offline"},
        "text": {"message": "radio.offline"}
      },
      {
        "type": "List",
        "items": {"bind": "state.stations"},
        "keyField": "ref",
        "itemTemplate": {
          "type": "MediaCard",
          "title": {"bind": "item.title"},
          "artwork": {"bind": "item.artworkHandle"},
          "primaryAction": {
            "command": "org.example.radio/play",
            "arguments": {"item": {"bind": "item.ref"}}
          }
        }
      }
    ]
  }
}
```

The message keys are looked up in declared localization files. The command must have been registered by that package and accept the declared item-reference schema. Artwork is a validated host asset handle. A card click does not confer arbitrary playback authority: the host creates an operation-specific gesture token and validates the resulting command under the normal playback/discovery contract.

Views declare state schemas, command argument schemas and loading/empty/error behavior. The renderer caps document depth, nodes, text bytes, image dimensions, items per page and patch frequency. Invalid updates leave the last valid state or an error surface; they cannot alter sibling content. Contexts are copied projections scoped to a route/modal/job, not live QObject references. A route gets no library or playback metadata unless separately granted.

## Published hooks

Each hook has a stable ID, major version, accepted contribution types, context schema, presentation constraints, lifecycle and multiplicity. These are the proposed first hooks, not arbitrary insertion coordinates:

| Hook | Multiplicity and contribution | Host-owned boundaries |
| --- | --- | --- |
| `home.sections@1` | Many typed section models | Core section layout, heading/card styles and section action semantics |
| `home.content@1` | One user-selected replacement content view | Library shell, navigation, Back/search contracts and recovery entry |
| `library.navigation@1` | Many namespaced routes in an extensions group | Core destinations and navigation hierarchy remain identifiable |
| `media.details.sections@1` | Many detail sections | Media target/revision, built-in identity and track controls |
| `subtitles.results.sections@1` | Many supporting sections | Host result selection, target association and attachment commands |
| `subtitles.search.content@1` | One user-selected replacement search body | Host modal frame, draft/selection contract, confirm/cancel and track assignment |
| `queue.item.actions@1` | Many commands | Current selected entry IDs, normal queue mutation checks and overflow ordering |
| `player.actions@1` | Many commands available to customization | Existing bounded toolbar zones, button sizing and playback-critical controls |
| `dialog.add-to-playlist.content@1` | One user-selected replacement picker | Target item set, allowed playlist handles and atomic confirm/cancel result |
| `settings.addons.sections@1` | Many package-specific settings sections | Core categories, Save/Cancel and account/permission controls |
| `actions.subtitles.open@1` | One user-selected alternate command | Existing action placement/accessible purpose and built-in fallback |
| `visualizer.surface@1` | One selected visualizer controller | Media canvas placement, overlays, transport chrome and render lifecycle |

New hooks require application work and a documented contract. Add-ons cannot create hooks inside core screens by naming a QML object, matching visible text or searching the widget tree.

Additive hooks have host-defined groups and optional relative ordering hints within that group. User ordering takes precedence. Otherwise use a stable contribution-ID tie-breaker; do not let add-ons request a universal “highest z-order” or reorder core controls. Each hook can cap visible contributions and use an overflow affordance.

The initial release can expose a subset of this table. A package checks optional hook availability and hides the dependent feature if absent. Required missing hooks prevent that contribution from activating. Announce hook availability through negotiation rather than letting authors infer it from Hakone version strings.

## New screens and modals

A `ui.route` contributes `addon/<packageId>/<routeId>`, a localized label/icon, typed parameters, state version and view resource. Open it from a declared navigation entry, home section or command. The host controls the title area, Back, search integration, loading/error surface and keyboard focus. Route parameters use scoped handles; a route string is not a filesystem or network URL.

The lifecycle is create, activate, suspend, resume and dispose. Suspending freezes updates unless an active background grant justifies them. Saved state is bounded, schema-versioned and nonsecret: selection, filters, search and scroll anchors are typical. Restoring state cannot grant fresh media access; handles must be reissued after authorization. Deep links to an unavailable add-on offer a core-owned unavailable screen and Back.

A `ui.modal` declares typed input/result schemas, preferred size classes, a view and standard actions. The host manages modal stacking, Esc/cancel, focus return and one completion. Add-ons return draft data/results; the host performs actual queue, track or preference changes through normal commands. A crash is cancellation or a recoverable error, never an indefinitely blocked core dialog.

## Replacing content and replacing commands

These are separate contracts:

- **Content replacement** supplies a view inside a known surface and must satisfy its model/state/result contract. A custom Home can arrange content inside the home area while retaining the application's navigation and recovery. A custom playlist picker returns valid playlist references, not arbitrary mutations.
- **Action replacement** chooses another registered command for a specifically overridable action. For example, the Subtitles button can open an enhanced search modal while retaining the button's recognizable purpose and host-owned placement. A new, unrelated command gets its own entry instead.

Installing a candidate only makes it available. The user chooses one replacement per surface/action in a core-owned “Interface integrations” setting. Default to built-in. If two packages advertise replacements, list both; package install order does not choose the winner. Replacement grants and selections are stored by the host and cannot be edited by add-on code.

Replacement contracts retain the original input/result semantics, cancellation, draft state and accessibility requirements. Do not expose replacements for permission prompts, account secrets, add-on recovery/update approval, OS file consent or the emergency stop/end-session controls. Keep a built-in “Open default…” command and recovery menu outside the replaced content.

On missing package, denied permission, unsupported contract, invalid state, crash or activation timeout, atomically restore built-in content or access to the built-in command and show a concise explanation. Content can fall back immediately. Do not automatically rerun a failed command when an effect may already have started; offer the default on the next explicit invocation, unless the host proves no effect began. Do not require navigating through the failed replacement to disable it. Withdraw leases and cancel effects before restoring a default. Preserve host-owned input drafts where valid.

## Player controls and customization

Reuse the redesign's bounded player layout zones. A `ui.command` declares its ID, label, icon, argument schema and optional checked/enabled/badge state. The `player.actions` hook offers it in the customization palette and an extensions overflow menu. The user chooses placement; install never silently rewrites their saved toolbar.

Persist namespaced IDs. A disabled/uninstalled command becomes an unavailable customization entry that can be removed or restored, while the active player renders a safe placeholder/overflow state. Do not reuse a removed extension's ID for a core action. Host-authorized availability predicates are descriptive; security checks still occur when the command executes.

Keyboard shortcuts are assigned by the host shortcut editor with conflict detection. Add-ons cannot intercept all keys, override protected shortcuts or claim an invisible global key listener.

## Theme contract

A theme is a declarative `theme` contribution with no executable logic or network access. It contains light/dark variants, semantic palette overrides, and constrained shape/effect tokens. Themes contain tokens only. Static raster images belong to a UI contribution's [asset map](sdk/static-ui-images.md). A theme cannot supply images, scripts, external URLs, fonts, QML, or shaders.

Example partial theme, merged with required host defaults:

```json
{
  "contract": "theme@1",
  "id": "midnight",
  "variants": {
    "dark": {
      "colors": {
        "surface.base": "#171923",
        "surface.panel": "#1e2230",
        "text.primary": "#edf0f7",
        "text.secondary": "#b6c0d4",
        "accent.primary": "#9ac6ff",
        "focus.ring": "#bbd9ff"
      },
      "shape": {"controlRadius": 6, "panelRadius": 10},
      "effects": {"panelOpacity": 0.96}
    }
  }
}
```

Use one resolved token snapshot for QML, QWidget dialogs, menus and all extension UI. Current QML and QWidget palettes duplicate values; that duplication needs refactoring before promising theme coverage. Components subscribe to a token revision so changing theme updates existing core and extension screens together.

Themes may adjust semantic colors, approved border/radius/shadow ranges and a small typography scale within host bounds. V1 keeps the host font family, icon meanings, spacing/layout density classes, minimum hit targets and layout geometry. Wider font/icon-pack customization can be a separately versioned capability later. A theme is not an unrestricted skins2 replacement.

Resolution order is host defaults, selected theme variant, explicit user accent choice, then authoritative accessibility/platform overrides. Unknown tokens fail package validation. Missing optional tokens fall back to host defaults. Unsupported light/dark variants use the built-in corresponding variant and explain that coverage in preview. User text size/UI scale, high contrast, focus visibility, reduced motion, reduced transparency and RTL behavior always win.

Validate semantic contrast pairs, focus indicators, alpha ranges and geometry bounds before activation. Preview safely with sample core and extension controls. Reject or correct invalid pairs through documented policy, not an unpredictable one-off stylesheet. Themes cannot hide required labels, alter slot IDs, move player controls, intercept input, change commands or modify other packages' data.

## Generated configuration

Configuration has a data schema plus optional form/layout hints. Use a documented JSON Schema subset for data, not arbitrary executable validators. Initial field types: boolean, integer/number with bounds, string, enum, multi-enum, locale/language list, duration, color, URL, host-picked file/folder handle, secret and account connection. Support defaults, required fields, bounded lists, groups, localized labels/help and declarative dependencies on other fields in the same package.

Example settings data schema:

```json
{
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "languages": {
      "type": "array",
      "items": {"type": "string"},
      "uniqueItems": true,
      "maxItems": 8,
      "default": ["en"],
      "x-hakone-control": "language-list",
      "x-hakone-label": "settings.languages"
    },
    "apiKey": {
      "type": "string",
      "minLength": 1,
      "x-hakone-control": "secret",
      "x-hakone-secret": true,
      "x-hakone-label": "settings.apiKey"
    }
  },
  "required": ["languages", "apiKey"]
}
```

`x-hakone-*` annotations belong to the proposed documented subset. In the host's persistent settings record, a secret is represented by a credential reference, not this input string; ordinary `settings.get` returns only presence/status. The input schema is used inside the host's protected editor and credential ingestion flow. Never send raw secret field input to an add-on's normal view-state dispatcher.

An existing valid credential reference satisfies a required secret field. The protected editor has distinct keep, replace and delete operations; an unchanged masked field does not require reentry. Deleting a required credential requires an explicit disconnect/unconfigure operation, or blocks ordinary Save with a nonsecret validation message. Never echo entered secret material in validation errors. Credential creation/deletion and the settings revision commit together, so cancel/failed save cannot delete the active key or leave a dangling reference.

All packages get a generated settings card under Add-ons and searchable labels/help. Relevant core screens can link to that same card or mount a scoped form using a published hook. A subtitle provider's card can also appear in subtitle provider preferences. Provider ranking is a core preference over contribution IDs and stays outside add-on-owned settings.

Reuse the current preference-row/backend pattern through an adapter supporting describe, value, stage, reset, discard and save. The adapter is restricted to the add-on's namespace. Form edits remain drafts. Cancel discards the draft; Save validates the complete object and atomically persists a revision. Config changes emit one revisioned event and invalidate affected jobs/caches. Restart-requiring changes are declared and shown before save. Existing dialogs must not commit unrelated changes merely because one provider is configured.

Support profile-wide defaults and account-specific settings in v1. Resolve values in this order: profile defaults, selected-account overrides, then explicit temporary session overrides. Credential references stay bound to their account and never inherit from another account. Media-specific rules are a future scope, not an implicit arbitrary database. Derived runtime state is not silently stored as preference. File/folder fields return scoped handles with separate user consent rather than giving general filesystem access.

## Accounts, secrets and custom setup

Common API-key, OAuth authorization-code/PKCE and device-code flows should be host-owned account recipes. Endpoints/origins and credential use are declared in the manifest; the host opens the system browser and handles callbacks, account status, disconnect and token refresh. Never ask an add-on view to impersonate the core permission/account UI. Providers can supply safe labels and account-status projections.

Store secrets in the platform credential facility or another reviewed secure store. If unavailable, explicitly offer session-only credentials; never quietly fall back to plaintext profile storage. Bind each credential handle to package/account and approved origin/header or broker auth recipe. The host injects it and prevents redirects from forwarding it to an unapproved origin. Logs and configuration export redact it.

Some protocols return tokens in JSON. A reviewed broker account recipe should extract/store the declared fields and return handles plus a redacted response. Do not forward an entire auth response to normal view state. Rare custom authentication can require a future explicitly sensitive API; it does not justify making all secrets generally readable. Update scope/key changes require ordinary permission/trust handling.

An advanced setup view may supplement generated settings when it needs a test connection, choose-region workflow or preview. It uses the same staged draft and standard host controls, and cannot bypass schema validation or credential handling. The basic form remains available for all declared ordinary fields.

## UI conformance

Before publishing a hook/component contract, test theme switching in an already open extension view, narrow/wide windows, enlarged UI/text, keyboard-only navigation, screen-reader labels, RTL, long translations and loading/empty/error states. Test focus restoration after modal cancel and failure. Test missing or crashed replacement providers and confirm that core recovery remains accessible. Core rendering rules should make compliant defaults easy; authors should not need to reimplement these behaviors for every add-on.
