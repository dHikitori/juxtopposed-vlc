# Normative schemas and author tools (Step 7)

Step 7 of the [continuation plan](../07-continuation-plan.md), first two
items. Written 31 October 2026. Author guide:
[author tools](../sdk/authoring-tools.md).

## Schemas and the shared corpus

Six Draft 2020-12 schemas are in `sdk/schemas/`: manifest, settings,
metadata result, discovery page, UI view and visualizer scene. Each lists
its host-only rules in `$comment`.

| Schema | Valid cases | Invalid cases |
| --- | --- | --- |
| manifest | 13 | 52 |
| settings | 11 | 47 |
| metadata-result | 17 | 27 |
| discovery-page | 16 | 25 |
| ui-view | 14 | 34 |
| visual-scene | 14 | 40 |

Two independent checks read the same 310 cases. `check-corpus.py` uses
jsonschema 4.23.0. `test_qt_addon_schema_corpus` uses the host's own
validators: `Manifest::parse`, `AddonUi.validateSettingsSchema` with the
account recipe check, `AddonUi.validateView`, `AddonVisualScene::validate`,
the worker's result envelope with `ResultRetention`, and a real
`AddonDiscoveryCoordinator` that must keep every row, revision, check
interval and cursor. Both pass all cases. The host test also has
`aFinalNewlineIsNeverPartOfAToken`, because Python's `re` shares PCRE's
`$` behavior and the corpus cannot hold those cases.

### Host gaps that the corpus found (fixed in `83f2123543`)

- `$` matched before a final newline in 59 add-on host patterns. A package
  ID, a credential header name (`"Authorization\n"`), an account client ID,
  a discovery revision and a scene color accepted a trailing newline. All
  end anchors are now `\z`.
- `https://@host` passed the user-info checks (19 sites).
- `AddonUi.safeKey` accepted non-strings (a missing view `id`, `id: true`).
- Numeric and Boolean discovery IDs were converted to text; a fractional
  `nextCheckSeconds` was truncated.
- The settings form did not type `default`, `enum`, `required` entries,
  condition fields or labels, and allowed `items` on a non-list field.
- The tool runner itself first passed values to `AddonUi.js` as Qt
  sequences, so `Array.isArray` was false and every `required` list was
  refused. The validator and the runner now pass plain JavaScript values.

Known host leniencies that stay (the schemas are stricter, and no corpus
case uses them): a node row's `kind` is ignored; an empty resource string
counts as "not set"; contribution `name` and `label` have no type check;
`credentials.use` with no bindings is accepted.

## Tools

| Tool | Test | Result |
| --- | --- | --- |
| `vlc-addon-validate` (meson, not installed) | `addon_validate_examples`, `addon_validate_refuses` | All ten examples valid; a package with an unknown permission refused |
| `vlc-addon-preview` | `addon_preview`, `addon_preview_theme` | A UI route with bound state, and the Midnight theme in four variants, render through the app components |
| `generate-rust.py` → `message-v1/src/contract.rs` | `addon_schema_rust_drift`; SDK `cargo test` (13 pass) | No drift; limits and enumerations come from the schemas |
| `Addon::self_test` in the SDK | SDK `a_self_test_message_calls_the_addon_check` | The dispatcher answers the install self-test |
| Templates (`metadata-provider`, `discovery-source`, `theme`) | `addon_templates` | Each builds, passes its unit tests and its real worker self-test, and validates |
| `check-corpus.py` | `addon_schema_corpus_python` | Skips without jsonschema 4.23.0; passes 310 cases with it |
| `check-ste.py` | (manual) | See the text audit |

All labels pass: profile 33 (plus one expected failure and one skip),
metadata 53, source 23, ui 9, app 6, session 4, visualizer 12.

Not done: macOS, and an installed location for the two tools (they build in
the meson tree only).
