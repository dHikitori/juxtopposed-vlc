#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate the raster app icons from share/icons/hakone.svg.

share/icons/hakone.svg is the full-colour logo and the source of truth.
share/icons/hakone-macos.svg is the macOS tile built around it.

Requires: pip install resvg-py pillow; macOS iconutil for hakone.icns.
"""

import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

import resvg_py
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, 'share', 'icons')


def square_svg(svg, margin=0.03):
    """Centre the logo in a square canvas, scaled to fit with a small margin."""
    view = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg)
    width, height = float(view.group(1)), float(view.group(2))
    body = svg[svg.index('>', svg.index('<svg')) + 1:svg.rindex('</svg>')]
    side = max(width, height) / (1 - 2 * margin)
    x, y = (side - width) / 2, (side - height) / 2
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {side} {side}">'
            f'<g transform="translate({x} {y})">{body}</g></svg>')


def render(svg, size):
    data = bytes(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size))
    return Image.open(io.BytesIO(data)).convert('RGBA')


def main():
    with open(os.path.join(ICONS, 'hakone.svg'), encoding='utf-8') as f:
        logo = square_svg(f.read())
    with open(os.path.join(ICONS, 'hakone-macos.svg'), encoding='utf-8') as f:
        tile = f.read()

    for size in (16, 32, 48, 128, 256):
        render(logo, size).save(os.path.join(ICONS, f'{size}x{size}', 'hakone.png'))

    for size, name in ((16, 'hakone16x16_xpm'), (32, 'hakone_xpm')):
        path = os.path.join(ICONS, f'{size}x{size}', 'hakone.xpm')
        # Pillow cannot write XPM; ImageMagick can.
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, 'icon.png')
            render(logo, size).save(png)
            subprocess.run(['magick', png, path], check=True)
        with open(path, encoding='utf-8') as f:
            xpm = re.sub(r'static char \*\w+\[\]', f'static const char *const {name}[]', f.read(), 1)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(xpm)

    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    images = [render(logo, size) for size in sizes]
    images[-1].save(os.path.join(ICONS, 'hakone.ico'), format='ICO',
                    sizes=[(s, s) for s in sizes], append_images=images[:-1])

    if shutil.which('iconutil'):
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, 'hakone.iconset')
            os.mkdir(iconset)
            for base in (16, 32, 128, 256, 512):
                render(tile, base).save(os.path.join(iconset, f'icon_{base}x{base}.png'))
                render(tile, base * 2).save(os.path.join(iconset, f'icon_{base}x{base}@2x.png'))
            subprocess.run(['iconutil', '-c', 'icns', '-o',
                            os.path.join(ICONS, 'hakone.icns'), iconset], check=True)
    else:
        print('iconutil not found: hakone.icns was not regenerated', file=sys.stderr)


if __name__ == '__main__':
    main()
