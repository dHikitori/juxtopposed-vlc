# Author tools

These tools use the app's own rules. None of them runs package code, except
the template check, which runs your module's self-test in the real worker.

## Schemas and corpus

[`schemas/`](schemas/) has the normative JSON Schemas (Draft 2020-12):

| Schema | Describes |
| --- | --- |
| `manifest.schema.json` | `addon.json` |
| `settings.schema.json` | The settings schema subset of the generated form, including account fields |
| `metadata-result.schema.json` | A metadata provider's `result` message, including `data.retention` |
| `discovery-page.schema.json` | The `data` of a roots, browse, search, suggest or subscribe reply |
| `ui-view.schema.json` | A `ui@1` view document |
| `visual-scene.schema.json` | A visualizer scene |

Each schema's `$comment` lists the host rules that JSON Schema cannot
express, for example totals across a document or a field that must name
another field. The host still applies them.

`schemas/corpus/<schema>/` has positive and negative cases. Two checks read
the same cases:

- `schemas/check-corpus.py` checks them against the schemas. Use
  `jsonschema` 4.23.0 in a separate tool environment; without it the check
  is skipped.
- `test_qt_addon_schema_corpus` checks them with the host's own validators
  (the manifest parser, the settings form, the view and scene validators,
  the result envelope and retention record, and a real discovery
  coordinator).

A valid case must pass both; an invalid case must fail both. Add a case when
you change a rule.

## Rust contract types

`schemas/generate-rust.py` writes `message-v1/src/contract.rs` from the
schemas: `Status`, `Keep`, `Retention`, `ItemKind`, `Item`, `Page`, `result`,
and the limits. `generate-rust.py --check` fails when the file and the
schemas differ.

The SDK also answers the install self-test. Implement `Addon::self_test`
and set `"runtime": {"selfTest": true}`. The default returns false.

## Package validator

```sh
vlc-addon-validate [--json] [--ui modules/gui/qt/player/qml/redesign/AddonUi.js] <package folder>...
```

It reads and hashes the declared files and parses the manifest. It checks
permissions, contributions, settings, themes and declared resources as an
install does. With `--ui` it also applies the settings form rules. Exit
status 0 means every package is valid. It does not run the module; the
install still runs the self-test.

## Package check for CI

`tools/check-package.py` applies the schemas and the install rules that do
not need the app, for a CI job without an app build. The validator above
stays the authority. See [Build and release](releasing.md).

## Preview

```sh
QT_QPA_PLATFORM=offscreen vlc-addon-preview --qml modules/gui/qt/player/qml/redesign \
    --output preview <package folder>
```

For each theme it writes sample controls in the dark and light variants,
also with high contrast. For each view (routes, modals, sections,
replacements and [player panels](player-panels.md)) it writes the document
with its declared initial state. The images use the app's own components.

## Templates

[`templates/`](templates/) has three packages to copy:

- `metadata-provider`: one HTTPS request, retention, a self-test and unit
  tests.
- `discovery-source`: pages, search, suggestions and a change feed.
- `theme`: a data-only theme.

`templates/check-templates.py` builds each one, runs its unit tests and
self-test, and validates the package.

For a player panel, start from the bundled [Episodes](https://github.com/hakone-org/hakone/tree/master/contrib-addons/episodes) package and the
[player panel guide](player-panels.md).
