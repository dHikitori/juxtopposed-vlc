# Local Windows build: background artwork work

The local review package at `build-windows/vlc-win64` contains clean source
`74ae4267fcabff4024e86a55b46557bed65a3cdd`. No release or push was made.
Restart the app to load this build.

The package includes background preparation of portable image requests, keys
and TLS, plus background cache reads, image decode and cache writes. It also
includes the earlier online subtitle responsiveness changes. The native
profile fence and durable generation helpers compile in the application.
Their peer control channel and production store bindings remain open. Native
metadata owner activation remains off.

## Validation

- Full Windows make build: successful exit, two jobs, 146.694 seconds.
- All 868 managed files match the checked candidate after publication.
- All 416 Windows executables and libraries pass architecture/import checks.
- All seven bundled add-ons and their 35 payloads match their sources.
- All 298 selected add-on, interface, subtitle and Core source inputs match
  the build cache. The cache was read only during this check.
- The packaged native worker loads the packaged TheSubtitleDB module. Without
  a key, it returns `auth_required`, makes no host calls and exits cleanly.
- The portable artwork cohort previously passed 271 normal and 271
  ASAN/UBSAN checks. The profile generation cohort passed 70 normal, 70
  ASAN/UBSAN and 66 native Windows checks. See the linked component records
  in [implementation status](../IMPLEMENTATION.md) for their scope and limits.

Publication replaced 417 managed files with atomic hardlink replacement.
Portable profile contents were not read or changed. The portable directory
and all recorded baseline hashes are unchanged. No Docker cache was deleted.
The minimum free-space reading during the build was 7,618,863,104 bytes.
Completed build logs were compressed losslessly; their hash mapping is retained.

No GUI test was performed. Startup and idle CPU, general UI response, the
Online providers dialog and the reported persistent cursor need manual
Windows review. These package checks do not prove that the cursor issue is
fixed. See [the cursor report](windows-cursor-feedback.md).

## Records

The final record is
`/tmp/hakone-windows-artwork-review-build/final-proof.json`, SHA-256
`2191e8b207defe3cbc1a86c73336a3fd41024008eaff3633289c1319fd2feffa`.
The same directory retains source pins, cache checks, package checks, worker
output, publication hashes and the build log.

| Published file | SHA-256 |
| --- | --- |
| `BUILD-INFO.json` | `d192a80bbf3dfc03f756ef54a267d1c6f1e621ef5934a458638a1d2505428881` |
| `vlc.exe` | `77909ebaa18857a196eaf24d7b66f1e73b3ba15e9a918c6ef8b914804da4d45f` |
| `plugins/gui/libqt_plugin.dll` | `3c04b85feb3699b3a46cb2caf35bdd14da0138b636bb4b4b05a4276e71cf5969` |
| `vlc-addon-worker.exe` | `7d2ed0999111a68ce672cc1e8b99ee2181a2d0f08b13acecb079c8e72d28af6d` |
