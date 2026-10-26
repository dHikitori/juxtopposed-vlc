#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write an optional consumer key to the build directory. Do not print the key."""
import argparse
import json
import os
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True)
args = parser.parse_args()
key = os.environ.get('OPENSUBTITLES_API_KEY', '').strip()
source = os.environ.get('OPENSUBTITLES_API_KEY_FILE', '')
if source:
    path = Path(source)
    try:
        text = path.read_text(encoding='utf-8').strip()
    except (OSError, UnicodeError):
        parser.error('The script cannot read the consumer key file.')
    assignment = re.compile(r'^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)$')
    filename = path.name.lower()
    dotenv_name = filename == '.env' or filename.startswith('.env.') or filename.endswith('.env')
    named_assignments = any(assignment.match(line.strip()) for line in text.splitlines())
    # A named assignment identifies a configuration file.
    # Do not copy another user secret into the consumer key.
    if '\n' not in text and not dotenv_name and not named_assignments:
        key = text
    else:
        key = ''
        for line in text.splitlines():
            entry = assignment.match(line.strip())
            if entry and entry.group(1) == 'OPENSUBTITLES_API_KEY':
                key = entry.group(2).strip().strip('"\'')
                break
        if not key:
            parser.error('The key file has no OPENSUBTITLES_API_KEY entry.')
if len(key) > 512 or any(not 0x21 <= ord(ch) <= 0x7e for ch in key):
    parser.error('The consumer key format is not correct.')
output = Path(args.output)
content = '// This file contains a local key. Do not commit this file.\n#pragma once\n'
content += 'static constexpr char subtitleConsumerKey[] = ' + json.dumps(key) + ';\n'
if not output.exists() or output.read_text(encoding='utf-8') != content:
    output.write_text(content, encoding='utf-8')
    output.chmod(0o600)
