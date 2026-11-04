# Episodes

The player panel with the other episodes of the playing series. The app
bundles it and turns it on; you can turn it off in Extensions like any
other add-on.

- **Contribution:** a `ui.panel` on `player.panels@1` with its view, icon
  and context command, and eight `ui.command` contributions for the
  view's controls.
- **Permissions:** `ui.contribute` (`player.panels@1`) and `media.siblings`
  (`folder`, `feed`). No network access.
- **What the app does:** finds the files of the same series in the media's
  folder, or the entries of its podcast feed; adds episode titles from
  metadata providers, watch state and artwork; plays the item you choose.
- **What the package does:** groups the items into seasons, episodes and
  versions; chooses the open row, the version, the marks and the play
  labels; pages long seasons.
- **Languages:** English, French, Spanish, Japanese, Chinese (Simplified)
  and Russian, in `locales/`. The view uses these messages, and the module
  embeds the same files for the text it builds, in the language the app
  sends with each command. Other languages use English.

## Build and test

```sh
./build.sh                      # dist/addon.wasm
cargo test                      # the panel rules, case by case
python3 -B tests/test_worker.py --worker <build>/modules/gui/qt/hakone-addon-worker
```

`tests/sample-state.json` is the state for a sample season. The Rust tests
keep it current (run them with `EPISODES_WRITE_FIXTURE=1` after a change),
and `test_qt_addon_schema_corpus` checks it against the app's view and
state rules.
