# Windows add-on loader repair

The first local Windows checkpoint at `c1cbc65816` contained all seven bundled
packages, but its loader rejected each package before installation. The
manifest loader appended the native Windows backslash to a Qt canonical path.
Qt canonical paths use forward slashes. The resource containment check then
refused even the package's own `addon.json`.

The loader now appends a forward slash and refuses an unavailable canonical
root. It retains the directory boundary, symbolic-link checks and resource
limits. Startup also imports TheSubtitleDB. The earlier startup list contained
the other six packages only.

An actual Windows command-line probe reproduced all seven failures with the
original compiled host. It returned 42 with zero installed packages. The same
probe, host archive, Core DLLs and packaged files passed after it linked the
corrected manifest object. All seven manifests loaded and all seven real
Manager imports succeeded. The corrected probe returned zero with seven
installed packages and no error output.

Both runs used a temporary store. They did not read or change the user's
portable profile. Exact commands, inputs and before/after records are in
`/tmp/hakone-win-addons-repro`. These checks prove the loader repair.

The complete local build at `600d3158b0` includes this change. Its first full
app check displayed a window but failed to create the expected temporary
package states. The user also reported an Extensions-tab hang. The complete
startup and responsiveness repair remains open; a passing import probe alone
does not prove that the app works. See [the current build record](windows-local-build.md).

The later `2a689c3a11` complete build passes the private full-app check with
all seven installed IDs, correct private paths, a responsive stable window
and normal shutdown. The original profile is unchanged by this new test.
The actual Extensions click remains a separate check. Cold startup still has
one initial hung-window sample before the stable window becomes responsive.
