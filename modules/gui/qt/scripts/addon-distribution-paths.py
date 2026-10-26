#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the build-tree directory for distribution add-ons."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
content = "#pragma once\n#define VLC_ADDON_REFERENCE_BUILD_ROOT " + json.dumps(str(args.root.resolve())) + "\n"
if not args.output.exists() or args.output.read_text() != content:
    args.output.write_text(content)
