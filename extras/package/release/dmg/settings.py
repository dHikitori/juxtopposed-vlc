# SPDX-License-Identifier: GPL-2.0-or-later
# dmgbuild settings for the drag-to-install Hakone disk image. dmgbuild executes
# this file with a `defines` mapping; extras/package/release/dmg.py passes the
# app, background and volume icon paths. Do not run it directly.
import os.path

application = defines['app']  # noqa: F821 (provided by dmgbuild)
appname = os.path.basename(application)

# LZFSE compression is readable on macOS 10.11 and later, well below the
# minimum macOS recorded in the build metadata.
format = 'ULFO'
filesystem = 'HFS+'

files = [application]
symlinks = {'Applications': '/Applications'}

# background@2x.png beside the 1x file becomes a HiDPI TIFF (tiffutil).
background = defines['background']  # noqa: F821
icon = defines['icon']  # noqa: F821

# Icon centres in window points; they line up with background.svg's arrow.
icon_locations = {appname: (170, 230), 'Applications': (470, 230)}
window_rect = ((200, 120), (640, 400))
default_view = 'icon-view'
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
show_icon_preview = False
arrange_by = None
icon_size = 128
text_size = 13
label_pos = 'bottom'
