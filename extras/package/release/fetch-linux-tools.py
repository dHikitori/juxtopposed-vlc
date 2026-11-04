#!/usr/bin/env python3
"""Fetch checksum-pinned deployment tools; never execute an unverified download."""
import argparse
import hashlib
from pathlib import Path
import time
import urllib.request

# Digests published with these GitHub release assets. A tagged release asset
# never changes. The continuous release may be replaced upstream; a replacement
# must be reviewed and its pin updated before use.
TOOLS = {
    'linuxdeploy': 'c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d',
    'linuxdeploy-plugin-qt': 'cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617',
}
# The release each tool is downloaded from. linuxdeploy's continuous build was
# replaced in November 2026, so it is pinned to its newest tagged release.
RELEASES = {
    'linuxdeploy': '1-alpha-20251107-1',
}


def url(tool, filename):
    release = RELEASES.get(tool, 'continuous')
    return f'https://github.com/linuxdeploy/{tool}/releases/download/{release}/{filename}'


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def fetch(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for tool, expected in TOOLS.items():
        path = directory / f'{tool}-x86_64.AppImage'
        if not path.exists():
            partial = path.with_suffix('.partial')
            try:
                request = urllib.request.Request(url(tool, path.name), headers={'User-Agent': 'Hakone-release-packaging'})
                for attempt in range(3):
                    try:
                        with urllib.request.urlopen(request, timeout=60) as source, partial.open('wb') as target:
                            for block in iter(lambda: source.read(1024 * 1024), b''):
                                target.write(block)
                        break
                    except OSError:
                        if attempt == 2:
                            raise
                        time.sleep(2 ** attempt)
                if digest(partial) != expected:
                    raise ValueError(f'Deployment tool checksum mismatch: {tool}')
                partial.replace(path)
            finally:
                partial.unlink(missing_ok=True)
        if digest(path) != expected:
            raise ValueError(f'Cached deployment tool checksum mismatch: {path}')
        path.chmod(0o755)
        print(f'{expected}  {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    fetch(args.directory)
