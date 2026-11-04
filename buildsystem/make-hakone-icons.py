#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate the raster app icons and installer art from share/icons/hakone.svg.

share/icons/hakone.svg is the full-colour logo and the source of truth.
share/icons/hakone-macos.svg is the macOS tile built around it. The Windows
installer bitmaps use the bundled Inter font for the product name.

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
FONTS = os.path.join(ROOT, 'modules', 'gui', 'qt', 'fonts')
INTER = [os.path.join(FONTS, f'Inter-{style}.otf') for style in ('Regular', 'SemiBold')]


def square_svg(svg, margin=0.03):
    """Centre the logo in a square canvas, scaled to fit with a small margin."""
    view = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg)
    width, height = float(view.group(1)), float(view.group(2))
    body = svg[svg.index('>', svg.index('<svg')) + 1:svg.rindex('</svg>')]
    side = max(width, height) / (1 - 2 * margin)
    x, y = (side - width) / 2, (side - height) / 2
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {side} {side}">'
            f'<g transform="translate({x} {y})">{body}</g></svg>')


def render(svg, size, height=None):
    data = bytes(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=height or size,
                                       font_files=INTER, skip_system_fonts=True))
    return Image.open(io.BytesIO(data)).convert('RGBA')


def placed_logo(svg, x, y, width):
    """The logo's paths scaled to width, with the top-left corner at x, y."""
    view = re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg)
    body = re.sub(r'<title>.*?</title>', '', svg[svg.index('>', svg.index('<svg')) + 1:svg.rindex('</svg>')])
    return f'<g transform="translate({x} {y}) scale({width / float(view.group(1))})">{body}</g>'


def installer_sidebar(logo, width, height):
    """The welcome and finish page art: the logo and name on the app's dark colour."""
    return (f'<rect width="{width}" height="{height}" fill="#1b1b1b"/>'
            + placed_logo(logo, (width - 96) / 2, 92, 96)
            + f'<text x="{width / 2}" y="204" text-anchor="middle" font-family="Inter" '
              'font-weight="600" font-size="22" fill="#ffffff">Hakone</text>'
            + f'<text x="{width / 2}" y="224" text-anchor="middle" font-family="Inter" '
              'font-size="12" fill="#9a9a9a">Media Player</text>')


def save_bmp(svg, width, height, path):
    image = render(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
                   f'viewBox="0 0 {width} {height}">{svg}</svg>', width, height)
    # Installers read plain 24-bit bitmaps; flatten onto white.
    flat = Image.new('RGB', image.size, '#ffffff')
    flat.paste(image, mask=image.getchannel('A'))
    flat.save(path, format='BMP')


def installer_art(logo):
    win32 = os.path.join(ROOT, 'extras', 'package', 'win32')
    # NSIS welcome and finish pages.
    save_bmp(installer_sidebar(logo, 164, 314), 164, 314,
             os.path.join(win32, 'NSIS', 'hakone_branding.bmp'))
    # WiX welcome and finish dialogs: art on the left, the dialog text on white.
    save_bmp('<rect width="493" height="312" fill="#ffffff"/>' + installer_sidebar(logo, 164, 312),
             493, 312, os.path.join(win32, 'msi', 'dlgbmp.bmp'))
    # WiX top banner: the dialog title is drawn over the white left side.
    # The pale snowcap needs the dark tile to stand out on white.
    save_bmp('<rect width="494" height="58" fill="#ffffff"/>'
             '<rect x="440" y="7" width="44" height="44" rx="10" fill="#1b1b1b"/>'
             + placed_logo(logo, 446, 16, 32),
             494, 58, os.path.join(win32, 'msi', 'bannrbmp.bmp'))


def main():
    with open(os.path.join(ICONS, 'hakone.svg'), encoding='utf-8') as f:
        source = f.read()
    logo = square_svg(source)
    installer_art(source)
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
        # The Cocoa interface and its DMG keep their own copies.
        for copy in (('modules', 'gui', 'macosx', 'Resources', 'App-Icons', 'hakone.icns'),
                     ('extras', 'package', 'macosx', 'dmg', 'disk_image.icns')):
            shutil.copyfile(os.path.join(ICONS, 'hakone.icns'), os.path.join(ROOT, *copy))
    else:
        print('iconutil not found: hakone.icns was not regenerated', file=sys.stderr)


if __name__ == '__main__':
    main()
