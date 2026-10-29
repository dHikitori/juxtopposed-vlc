# Local Windows build: online subtitle responsiveness

The usual local package at `build-windows/vlc-win64` now contains clean source
`335ac989d0a6b374cd47fbea5ec3e18920b46a17`. This is a local review build.
No release, push or PR was made.

The [subtitle fix](subtitle-online-responsiveness.md) is in `f25de2c902`.
The next commit, `335ac989d0`, corrects two translation-event test calls that
used a GUI application cast in a core-only test process. Production code is
the same in both commits.

## Validation

- Full Windows make build: 133.177 seconds, two jobs, successful exit.
- Contrib revision: `4ca4eb0c3e3d1181a48f1e216f4324c6e2670ef7`.
- All 868 managed package files verified after publication.
- All 416 Windows executables and libraries pass architecture/import checks.
- All seven reference add-ons and their 35 payload files match their sources.
- The packaged Windows worker loads the packaged TheSubtitleDB module. With
  no key, it returns `auth_required`, makes no host calls and exits cleanly.
- The six related WSL suites pass 349 normal and 349 ASAN/UBSAN checks, with
  no failed or skipped cases and no runtime warnings. The normal producer
  cohort has 87 fresh objects; the sanitizer dependency closure has 38.
- The current subtitle suite was rebuilt and rerun after the test correction.
  All 87 normal producer object hashes still match the original proof.

The compiler log confirms fresh compilation of prepared invocations, metadata
selection, the shared metadata service and the subtitle service. The package
uses the previously verified Windows worker, whose source fingerprints are
unchanged.

## Publication and preserved data

Publication used the managed-file manifest and atomic replacement. It did not
read or replace portable profile contents. The portable directory, the retained
`fd3` baseline and the earlier build before add-ons remain intact. No Docker
cache files were deleted. The lowest free-space reading during the build was
7,830,085,632 bytes.

Restart the app before testing **Online providers**. A process that is already
open still has the earlier code loaded. The actual shared-service regression
keeps its event loop responsive during a blocked provider read. Manual Windows
GUI confirmation remains open: computer-use initialization failed for this
WSL workspace, so no GUI responsiveness measurement is claimed here.

## Records

The final record is
`/tmp/hakone-windows-subtitle-hang-build/final-proof.json`, SHA-256
`56c1bb9a9baa680b68de1044b9944005ba1c52fc8577014cf4216dc82cf25c85`.
Package validation is in that directory's `validation/` folder. Test input,
object, program and archive records are in `/tmp/hakone-subtitle-online-proof/`.
Completed test outputs are retained in verified compressed archives.

| Published file | SHA-256 |
| --- | --- |
| `BUILD-INFO.json` | `5bdb331d55c8aaa8e905802e6952961866a3919979313fec0a35da5c996130e1` |
| `vlc.exe` | `f18f71cf2d1522d64a4a36e8c0bca2275b08e0e28d6a9251075b61f568b6c3a8` |
| `plugins/gui/libqt_plugin.dll` | `6f9509cba6aaad35663b086f30394c558f1639133861173f0fb45ecec2ad727a` |
| `vlc-addon-worker.exe` | `7d2ed0999111a68ce672cc1e8b99ee2181a2d0f08b13acecb079c8e72d28af6d` |
