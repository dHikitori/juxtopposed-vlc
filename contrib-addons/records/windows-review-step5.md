# Windows review package (Step 5)

Step 5 of the [continuation plan](../07-continuation-plan.md). Built
30 October 2026 into `build-windows/vlc-win64`. Its `portable/` settings
were kept. Nothing was released or pushed.

## Package

| Part | Source | SHA-256 |
| --- | --- | --- |
| Core and plugins | `8b78e46210` (Step 4) | `vlc.exe` `cd5d5473df08e6c6ba231e80863e2bc74e4518fb91b25041403f27b52260352c` |
| Qt plugin | `62bfe1513e` (launch logging) | `plugins/gui/libqt_plugin.dll` `5e4bbd8e9bef9561e78eaf201e61803236fbdcc88e150d2f74da04c76086d7d4` |
| Add-on worker | `8b78e46210`, Rust 1.99 host stable, `x86_64-pc-windows-gnu` | `vlc-addon-worker.exe` `daf92838d58ba4d8a4a466898f0bf6354413bfe6871106696002e0d100a1f35a` |

Build: `build-windows/build.sh make`, then `qt`, two jobs, contrib
`4ca4eb0c3e`, with `VLC_WINDOWS_RUST_TOOLCHAIN_DIR` set to the host stable
toolchain. The `addon_worker` block in `BUILD-INFO.json` still names the
previous verified worker bundle; this package's worker was built in the
container from current source.

## Checks run here

The packaged worker ran natively on Windows with the bundled MusicBrainz
module (times include about 0.3 s of WSL launch overhead):

| Check | Result |
| --- | --- |
| `--precompile` | Exit 0 in 1.09 s; 808 KB compiled module |
| `--precompiled` with the right digest | Ready, `precompiled: true`, 0.32 s |
| From source | Ready, 0.62 s |
| `--precompiled` with a wrong digest | Ready, `precompiled: false` (falls back to source) |

The app itself was not started here, so that your `portable/` profile is not
changed.

## Your checks

Open Tools › Messages and set verbosity to 2 (debug) for checks 4 and 5.

1. **Extensions click.** Open Settings › Extensions. The list shows without
   a hang. A package whose worker crashed shows "Stopped unexpectedly".
2. **Cached start.** Quit, then start again. Startup is not slower than
   before; the bundled add-ons are not prepared again.
3. **Network-file Close.** Play an audio file from a network share, then
   Close. The app exits promptly.
4. **Cursor.** Use the app normally for a few minutes. Each helper start
   logs "Add-on helper launch N: <program>". Note whether the spinner cursor
   appears, and whether N rises while it does. Every helper now starts with
   `STARTF_FORCEOFFFEEDBACK`.
5. **Owner on.** Start with `vlc.exe --metadata-native-owner`, with
   Settings › Privacy › metadata network access on. Play a tagged music file.
   Artwork appears, and the log shows helper launches for
   `vlc-addon-worker.exe` while it is fetched.
6. **Subtitle53.** The plan names this check, but this repository does not
   define it. Run it as you did before.

Step 5 exits when you confirm these checks, or when a failing check has a
specific defect and a way to reproduce it.
