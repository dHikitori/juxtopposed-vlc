#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the versioned update metadata included in published release notes."""
import json
import re

from version import ROOT, parse_version, source_generation

MARKER = '<!-- vlc-update-metadata'


def release_metadata(version, root=ROOT):
    parse_version(version)
    path = root / f'doc/releases/{version}.json'
    overrides = json.loads(path.read_text()) if path.exists() else {}
    if not isinstance(overrides, dict) or overrides.keys() - {'product_name', 'package_prefix', 'subtitle'}:
        raise ValueError(f'{path}: expected only product_name, package_prefix and subtitle')
    metadata = dict(schema=1, version=version, generation=source_generation(root),
                    product_name='Hakone', package_prefix='hakone', subtitle='')
    metadata.update(overrides)
    for key, limit in (('product_name', 128), ('subtitle', 512)):
        value = metadata[key]
        if (not isinstance(value, str) or value != value.strip() or
                (not value and key == 'product_name') or
                any(not char.isprintable() for char in value) or
                len(value.encode('utf-16-le')) // 2 > limit):
            raise ValueError(f'{path}: {key} must be a plain single line of at most {limit} characters')
    prefix = metadata['package_prefix']
    if not isinstance(prefix, str) or len(prefix) > 64 or not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', prefix):
        raise ValueError(f'{path}: package_prefix must contain lowercase words or numbers separated by hyphens')
    return metadata


def metadata_block(metadata):
    # Escape comment delimiters without changing the decoded display strings.
    payload = json.dumps(metadata, ensure_ascii=True, separators=(',', ':'))
    payload = payload.replace('<', '\\u003c').replace('>', '\\u003e').replace('&', '\\u0026')
    if len(payload.encode()) > 4096:
        raise ValueError('Update metadata exceeds 4096 bytes')
    return f'{MARKER}\n{payload}\n-->'


def append_metadata(notes, metadata):
    if MARKER in notes:
        raise ValueError('Release notes already contain update metadata; use the release JSON file')
    return notes.rstrip() + '\n\n' + metadata_block(metadata) + '\n'
