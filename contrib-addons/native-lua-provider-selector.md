# Native Lua provider selector

The owned local stage must set `META_FETCHER_PROVIDER_NONE` before it invokes the Lua metadata or artwork module. This value returns before Lua state creation and before script execution. The native folder artwork module and local metadata readers remain available.

A named native compatibility adapter can select MusicBrainz, Google Images, French TV, or Last.fm. The selector accepts only the exact known `.lua` or `.luac` name under the app's system data or library directory. A same-name script in the user directory cannot enter this route. A named local-scope call cannot execute a network provider script. Unknown selectors fail closed. `META_FETCHER_PROVIDER_ANY` retains the unowned legacy batch behavior.

This selector grants no network authority. The native request must still have its consent, cancellation guard, and native input restrictions. A named provider must run on a private working item. It cannot write a live target or pass a raw image address to the UI.

No provider family is removed. The new native selector and named-adapter API do not activate the shared MainUI metadata owner. Local artwork proof, native format parity, the parent preparser handoff, and native provider selection remain separate gates.

## Isolated fixture recipe

The pure fixture needs only the configured VLC headers:

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

The runtime fixture uses the real Lua module. First build the normal `modules/liblua_plugin.so` target through the coordinated native build. The configured Lua headers need this existing Debian include directory:

```sh
export C_INCLUDE_PATH="$PWD/.design-reference/libplacebo-blur-audit/deps/usr/include/x86_64-linux-gnu${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
```

Then compile and run the small fixture:

```sh
cc -std=gnu17 -DHAVE_CONFIG_H \
  -Ibuild-redesign -Ibuild-redesign/include -Iinclude -I. \
  -Icompat/stdbit -Icompat/stdckdint -Wall -Wextra \
  -Werror-implicit-function-declaration \
  test/modules/lua/meta_selector_runtime.c \
  -o /tmp/vlc-metadata-pipeline/meta_selector_runtime \
  -Lbuild-redesign/lib -Lbuild-redesign/src -lvlc -lvlccore -pthread
VLC_PLUGIN_PATH="$PWD/build-redesign/modules" \
  /tmp/vlc-metadata-pipeline/meta_selector_runtime
```

Before the normal module rebuild, the same fixture passed against `/tmp/vlc-lua-selector-overlay/plugins/liblua_plugin.so`. That isolated module kept the normal module link closure and replaced only the current `meta.c` and `vlc.c` objects. No shared build ran for that proof. To repeat that exact retained artifact run, set `VLC_PLUGIN_PATH=/tmp/vlc-lua-selector-overlay/plugins` in the last command. A normal module run remains necessary after registration.

The runtime fixture has six provider/scope probes. Each test script writes a distinct marker at file scope, before its descriptor or fetch function. This detects execution of an unselected script. Positive controls prove that the selected system file and the unowned user-file path can execute. The fixture uses no network service. The temporary scripts and markers are removed on success. The runtime fixture returns 77 on Windows; the pure path fixture has no platform I/O.
