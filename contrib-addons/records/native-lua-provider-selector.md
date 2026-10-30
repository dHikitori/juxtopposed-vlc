# Native Lua metadata selection

Native Lua metadata loading accepts app-shipped files only. This rule applies
before and after metadata ownership, including a closed or retired owner. No
reader profile, input option, or child IPC field can restore an arbitrary Lua
metadata script.

The reader accepts only `lua/meta/reader/filename.lua` and `filename.luac` under
the app's system library or data directory. It rejects each other path before
Lua state creation or script execution. A same-name file in the user directory
cannot enter this route. Native binary metadata readers remain available.

The shipped filename script performs no I/O. It reads the URL-decoded URI
basename. An existing title, including an empty title, stops its work. Its
case-sensitive `S<digits>E<digits>` match writes the title and the legacy extra
keys `showName`, `seasonNumber`, and `episodeNumber`. This gate does not convert
those keys to the standard `show_name`, `season`, or `episode` fields.

A native compatibility adapter can select MusicBrainz, Google Images, French
TV, or Last.fm. The artwork selector accepts only their four exact `.lua` or
`.luac` names under the system library or data directory. `ANY` traverses that
same closed set. User files and unknown system files cannot execute. A local
scope call cannot execute these network artwork scripts. `NONE` returns before
state creation. Unknown selectors fail closed.

Core ships no Lua metadata fetcher script. `FetchMeta` therefore returns before
it scans that directory or creates a Lua state. Separate add-ons supply new
metadata providers.

The filename VM has the base and string libraries, item methods, and messages.
The artwork VM also has table, math, string helpers, and the native VLC stream
API. Neither VM has `io`, `os`, `package`, `debug`, file loaders, code loaders,
`require`, an external module path, or VLC object/variable/XML bindings. The
four retained artwork scripts need none of those removed APIs. The host loader
can still load their selected app-shipped source or bytecode.

This selection grants no network authority. Artwork work still needs native
consent, cancellation, legacy ownership admission, and input restrictions. A
named adapter must use a private item. It cannot publish a raw image address
to the UI. No provider file or family is removed. The MainUI Core metadata
owner remains inactive until its other compatibility and consumer gates pass.

## Scoped playlist inputs

The Lua playlist module refuses every object that has a native input policy.
The refusal occurs before allocation, VM initialization, and script probes.
Inherited and revoked policies also cause refusal. Raw Lua I/O and OS APIs
cannot receive the authority of a contained source input.

Ordinary unscoped native playlists keep their existing Lua APIs and parser.
No Core playlist script is removed. The actual module fixture proves that an
unscoped playlist executes a top-level file marker and returns its native child
item. Scoped, inherited, and revoked cases have no marker and no Lua demux.
All four cases use an in-memory source and no network service.

## Fixtures

The pure selector fixture covers both system roots, source and compiled file
names, all four artwork selectors, `ANY`, `NONE`, unknown selectors, user paths,
sibling roots, wrong directories, suffixes, missing paths, and the filename
reader allowlist.

The actual Lua module fixture has 27 probes. It tests pre-init rejection through
top-level item metadata writes. Thus denial of file I/O cannot produce a false
pass. Positive controls prove selected artwork and reader execution and the
small VM API set. The fixture also tests the actual shipped filename source,
genuine compiled bytecode, title preservation, URI decoding, exact legacy keys,
case, no-match input, active and closed owners, and Lua-disabled behavior. It
uses no live service. Temporary fixture files are removed after success.

The fixture gives `ANY` one successful trusted candidate. A separate probe
removes all trusted candidates and proves that unknown and user scripts still
cannot execute. This test does not depend on native directory sort order.
Named selector probes retain competing system files.

## Isolated recipe

The pure fixture needs the configured VLC headers:

```sh
source .design-reference/native-build/env.sh
mkdir -p /tmp/vlc-metadata-pipeline
cc -std=gnu17 -DHAVE_CONFIG_H \
  -Ibuild-redesign -Ibuild-redesign/include -Iinclude -I. \
  -Icompat/stdbit -Icompat/stdckdint -Wall -Wextra \
  test/modules/lua/meta_selector.c \
  -o /tmp/vlc-metadata-pipeline/meta_selector
/tmp/vlc-metadata-pipeline/meta_selector
```

For the runtime fixture, build the normal Lua plugin and the compiled filename
asset through the coordinated build. The targets are `modules/liblua_plugin.so`
and `modules/lua/meta/reader/filename.luac`. The configured Lua headers also need
the existing multiarch include directory:

```sh
export C_INCLUDE_PATH="$PWD/.design-reference/libplacebo-blur-audit/deps/usr/include/x86_64-linux-gnu${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
cc -std=gnu17 -DHAVE_CONFIG_H \
  -DTOP_SRCDIR=\"$PWD\" -DTOP_BUILDDIR=\"$PWD/build-redesign\" \
  -Ibuild-redesign -Ibuild-redesign/include -Iinclude -I. \
  -Icompat/stdbit -Icompat/stdckdint -Wall -Wextra \
  -Werror-implicit-function-declaration \
  test/modules/lua/meta_selector_runtime.c \
  -o /tmp/vlc-metadata-pipeline/meta_selector_runtime \
  -Lbuild-redesign/lib -Lbuild-redesign/src -lvlc -lvlccore -pthread
VLC_PLUGIN_PATH="$PWD/build-redesign/modules" \
  /tmp/vlc-metadata-pipeline/meta_selector_runtime
```

`VLC_TEST_FILENAME_LUAC` can select a separately compiled copy of the shipped
filename script. The fixture verifies the bytecode signature. `VLC_TEST_VERBOSE=1`
adds native module logs for diagnosis. The runtime fixture returns 77 on Windows;
the pure selector fixture needs no platform I/O.

The isolated overlay keeps the normal Lua module link closure. It replaces the
current `meta.c` and `stream_filter.c` objects. It does not replace Core or run a
shared build. Its recipe and output are in `/tmp/vlc-lua-reader-overlay/`.

The fresh normal Lua module, compiled filename asset, pure selector target, and
runtime target also pass. The Meson runtime target depends on both the module
and that compiled asset. Automake selects its own compiled asset path. These
checks preserve native filename and playlist behavior. They do not activate
the shared metadata owner.
