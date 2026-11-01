# Add-ons

Add-ons extend the player without changing its code. An add-on is a package
folder with an `addon.json` manifest, its declared files, and usually a
portable WebAssembly module. The app runs each module in a separate worker
process with a fixed memory and time budget. A module has no file, network or
player access of its own: it asks the app, and the app checks every request
against the permissions that the user accepted for that package.

An add-on can contribute:

- **Metadata providers:** episode titles, artwork and subtitles.
- **Player panels and commands:** views that the app draws from JSON
  documents, with buttons in the player layout.
- **Media sources:** browse and search that play through the app.
- **Themes and visualizers:** data-only packages with no module.

The user installs, updates, turns off and removes add-ons in
**Extensions**. Each install and each update that asks for more shows the
permissions to accept first.

## Bundled add-ons

The app ships these packages and turns them on in a new profile. The user
can turn each one off in Extensions.

| Add-on | What it does |
| --- | --- |
| [Episodes](episodes) | A player panel with the other episodes of the playing series, from its folder or podcast feed. |
| [TVmaze episode titles](tvmaze) | Episode titles for TV series from [TVmaze](https://www.tvmaze.com/api). |
| [TheSubtitleDB](thesubtitledb) | Subtitle search for movies and episodes. It needs your own API key in its settings. |

Each folder holds the package files and the source of its module. Its
README describes how to build and test it.

## Write an add-on

The [add-on SDK](sdk/README.md) describes the manifest, the message
protocol, the view documents and each contribution type. It also has the
schemas, the Rust message crate, and [templates](sdk/templates) to start
from. Use `vlc-addon-validate` and `vlc-addon-preview` from a build to
check a package before you install it.
