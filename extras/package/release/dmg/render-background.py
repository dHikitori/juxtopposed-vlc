#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render the disk image background (1x and @2x PNG) from background.svg.

The PNGs are committed so release builds do not need these tools. After
editing the SVG, re-render in a throwaway virtual environment:

    python3 -m venv /tmp/dmg-art
    /tmp/dmg-art/bin/pip install resvg-py==0.5.0 pillow==13.0.0
    /tmp/dmg-art/bin/python extras/package/release/dmg/render-background.py

Text uses the bundled Inter fonts only (system fonts are ignored) so the
result does not depend on the machine that renders it.
"""
import io
from pathlib import Path

from PIL import Image
import resvg_py

HERE = Path(__file__).resolve().parent
FONTS = HERE.parents[3] / 'modules/gui/qt/fonts'


def render(scale):
    png = resvg_py.svg_to_bytes(
        svg_path=str(HERE / 'background.svg'), zoom=scale, skip_system_fonts=True,
        font_files=[str(FONTS / 'Inter-SemiBold.otf'), str(FONTS / 'Inter-Regular.otf')])
    image = Image.open(io.BytesIO(bytes(png))).convert('RGB')
    if image.size != (640 * scale, 400 * scale):
        raise SystemExit(f'Unexpected {scale}x size: {image.size}')
    # Finder pairs the two representations by resolution (tiffutil -cathidpicheck).
    name = 'background.png' if scale == 1 else f'background@{scale}x.png'
    image.save(HERE / name, dpi=(72 * scale, 72 * scale), optimize=True)
    return HERE / name


if __name__ == '__main__':
    for scale in (1, 2):
        print(render(scale))
