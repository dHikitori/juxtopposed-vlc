#!/usr/bin/env python3
"""Check that relative Markdown links in contrib-addons resolve.

Each link target must exist. A #fragment after a Markdown file must match a
heading in that file. External links (with a URL scheme) are not checked.
"""

import re
import sys
from pathlib import Path

LINK = re.compile(r'(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
FENCE = re.compile(r'^\s*(```|~~~)')


def anchors(path):
    found = set()
    fenced = False
    for line in path.read_text(encoding='utf-8').splitlines():
        if FENCE.match(line):
            fenced = not fenced
        if fenced or not line.startswith('#'):
            continue
        title = line.lstrip('#').strip().lower()
        title = re.sub(r'[^\w\- ]', '', title).replace(' ', '-')
        slug = title
        number = 1
        while slug in found:
            slug = f'{title}-{number}'
            number += 1
        found.add(slug)
    return found


def links(path):
    fenced = False
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced:
            continue
        line = re.sub(r'`[^`]*`', '', line)
        for match in LINK.finditer(line):
            yield number, match.group(1)


def main():
    root = Path(__file__).resolve().parent
    errors = []
    checked = 0
    cache = {}
    for path in sorted(root.rglob('*.md')):
        if any(part in ('target', 'node_modules') for part in path.parts):
            continue
        for number, target in links(path):
            if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.I):
                continue
            checked += 1
            file_part, _, fragment = target.partition('#')
            resolved = (path.parent / file_part).resolve() if file_part else path
            where = f'{path.relative_to(root)}:{number}'
            if not resolved.exists():
                errors.append(f'{where}: {target} does not exist')
                continue
            if fragment and resolved.suffix == '.md':
                if resolved not in cache:
                    cache[resolved] = anchors(resolved)
                if fragment.lower() not in cache[resolved]:
                    errors.append(f'{where}: {target} has no heading #{fragment}')
    for error in errors:
        print(error, file=sys.stderr)
    print(f'{checked} relative links checked, {len(errors)} broken')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
