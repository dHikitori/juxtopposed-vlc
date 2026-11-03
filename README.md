# Hakone Media Player

**Hakone** is a fork of [VLC media player](https://www.videolan.org/vlc/) by
[VideoLAN](https://www.videolan.org/). It adapts
[Juxtopposed’s VLC redesign](https://www.figma.com/community/file/1691465999134370980/vlc-redesign)
for VLC’s native interface. The interface and settings workflow are in active development.

Download Hakone from [hakone.org](https://hakone.org/) or the
[Releases page](https://github.com/hakone-org/hakone/releases).
Release tooling and the `4.0.0-beta.N` version scheme are documented in [doc/RELEASING.md](doc/RELEASING.md).
For building on Linux from source, see [doc/BUILD-linux.md](doc/BUILD-linux.md).
For building on macOS from source, see [doc/BUILD-macos.md](doc/BUILD-macos.md).
For building on Windows from source, see [doc/BUILD-win32.md](doc/BUILD-win32.md).

![Simple player with the pixel-cat video](doc/images/redesign/player.png)

| Library sidebar | Video settings |
| --- | --- |
| ![Library sidebar over the video](doc/images/redesign/sidebar.png) | ![Video settings over the video](doc/images/redesign/video-settings.png) |

**Hakone** is a libre and open source **media player** built on the VLC
**multimedia engine**, which is focused on **playing everything**, and **running everywhere**.

Like VLC, **Hakone** can play most multimedia files, discs, streams, devices and is also able to
convert, encode, **stream** and manipulate streams into numerous formats.

The **engine of VLC** can be embedded into 3rd party applications, and is called *libVLC*.
Hakone keeps the libVLC name and API.

## About VLC

**VLC** is part of the [VideoLAN project](https://videolan.org) and
is developed and supported by a community of volunteers.

The VideoLAN project was started at the university [École Centrale Paris](https://www.centralesupelec.fr/) who
relicensed VLC under the GPLv2 license in February 2001. Since then, VLC has
been downloaded **billions** of times.

## License

**VLC** is released under the GPLv2 *(or later)* license.
*On some platforms, it is de facto GPLv3, because of the licenses of dependencies*.

**libVLC**, the engine is released under the LGPLv2 *(or later)* license. \
This allows embedding the engine in 3rd party applications, while letting them to be licensed under other licenses.

**Hakone** is a modified version of VLC and is distributed under the same terms:
the application under the GPLv2 *(or later)* license, and libVLC under the
LGPLv2 *(or later)* license. VLC's licenses and copyright notices still apply
(see [COPYING](COPYING) and [COPYING.LIB](COPYING.LIB)).

## Redesign attribution

The redesigned interface on `master` is adapted from **VLC Redesign**
by [Juxtopposed](https://www.juxtopposed.com/), licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
The original design is available on
[Figma Community](https://www.figma.com/community/file/1691465999134370980/vlc-redesign),
with a [video walkthrough](https://www.youtube.com/watch?v=ERFeOgqk324).

This implementation modifies the design for VLC's native interface, including
responsive layouts, interactive controls, settings organization, and integration
with VLC's playback and media library features. The design credit and CC BY 4.0
notice apply to the original design; the software licenses remain as described
above.

The interface uses [Inter](https://rsms.me/inter/) by Rasmus Andersson and contributors.
The bundled fonts use the [SIL Open Font License 1.1](modules/gui/qt/fonts/LICENSE.txt).

# Platforms

Hakone builds are currently available for:
- Windows *(x64)*
- macOS *(Apple Silicon and Intel)*
- GNU/Linux *(x86_64)*

Get them from [hakone.org](https://hakone.org/) or the
[Releases page](https://github.com/hakone-org/hakone/releases).
To build from source, see [doc/BUILD-linux.md](doc/BUILD-linux.md),
[doc/BUILD-macos.md](doc/BUILD-macos.md) and [doc/BUILD-win32.md](doc/BUILD-win32.md).

## Upstream VLC

If you are looking for VLC media player itself (including the Android and iOS apps
and the other platforms VLC supports), get it from
[VideoLAN's website](https://www.videolan.org/vlc/).

# Contributing & Community

Hakone is developed on GitHub at
[github.com/hakone-org/hakone](https://github.com/hakone-org/hakone).
Please report bugs and send pull requests there, not to VideoLAN.

The main development of VLC is done in the C language, but this repository also contains
plenty of C++, Obj-C, asm and Rust. The Hakone interface is written in C++ and QML on Qt.

We need help with the following tasks:
- Coding
- Packaging for Windows, macOS and Linux distributions
- Technical writing for the documentation
- Design
- Support

Please contribute :)

## Contributing to upstream VLC

**VLC** is maintained by a community of people, and VideoLAN is not paying any of them.
Changes to the VLC engine that are not specific to Hakone are best contributed upstream
through Merge Requests on VideoLAN's [GitLab repository](https://code.videolan.org/videolan/vlc/).
The VideoLAN community is on IRC, in the **#videolan** channel on *[Libera.chat]*.

[Libera.chat]: https://libera.chat

# libVLC

**libVLC** is an embeddable engine for 3rd party applications and frameworks.

It runs on the same platforms as VLC *(and sometimes on more)* and can provide playback,
streaming and conversion of multimedia files and streams.


**libVLC** has numerous bindings for other languages, such as C++, Python and C#.

# Support

## Links

Some useful links that might help you:

- [Hakone web site](https://hakone.org/)
- [Hakone releases](https://github.com/hakone-org/hakone/releases)
- [Hakone bug tracker](https://github.com/hakone-org/hakone/issues)

Upstream VLC resources (for the VLC engine, not for Hakone support):

- [VLC web site](https://www.videolan.org/vlc/)
- [VLC wiki](https://wiki.videolan.org/)
- [VLC Developer's Corner](https://wiki.videolan.org/Developers_Corner)
- [VLC hacking guide](https://wiki.videolan.org/Hacker_Guide)
- [VideoLAN web site](https://www.videolan.org/)

## Source Code sitemap
```
ABOUT-NLS          - Notes on the Free Translation Project.
AUTHORS            - VLC authors.
COPYING            - The GPL license.
COPYING.LIB        - The LGPL license.
INSTALL            - Installation and building instructions.
NEWS               - Important modifications between the releases.
README             - Project summary.
THANKS             - VLC contributors.

bin/               - Hakone binaries.
bindings/          - libVLC bindings to other languages.
compat/            - compatibility library for operating systems missing
                     essential functionalities.
contrib/           - Facilities for retrieving external libraries and building
                     them for systems that don't have the right versions.
doc/               - Miscellaneous documentation.
extras/analyser    - Code analyser and editor specific files.
extras/buildsystem - Different build system specific files.
extras/misc        - Files that don't fit in the other extras/ categories.
extras/package     - Packaging specific files such as spec files.
extras/tools/      - Facilities for retrieving external building tools needed
                     for systems that don't have the right versions.
include/           - Header files.
lib/               - libVLC source code.
modules/           - VLC plugins and modules. Most of the code is here.
                     The Hakone interface is in modules/gui/qt.
po/                - Translations.
share/             - Common resource files.
src/               - libvlccore source code.
test/              - Testing system.
```
