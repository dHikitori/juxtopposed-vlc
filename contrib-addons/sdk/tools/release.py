#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tag a release of an add-on, with its built files, without committing them to the branch.

The app installs a release from the files of the tagged commit. A built
module such as dist/addon.wasm must be in that commit, but it does not
belong in the branch history. This tool:

1. reads the version from addon.json, and refuses a tag that exists;
2. makes a commit on top of HEAD (not on the branch) that adds every file
   that addon.json declares and Git does not track, for example an
   ignored dist/addon.wasm;
3. puts an annotated tag v<version> on that commit, and with --push sends
   only the tag to the remote.

The branch does not change. Run it from a clean checkout, after the build.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def git(root, *args, capture=True, check=True, env=None):
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=capture, text=True, check=False, env=env)
    if check and result.returncode != 0:
        sys.exit(f"release: git {' '.join(args)} failed: {(result.stderr or '').strip()}")
    return result


def tag_exists(root, tag, remote):
    if git(root, "rev-parse", "-q", "--verify", f"refs/tags/{tag}", check=False).returncode == 0:
        return True
    if remote:
        listing = git(root, "ls-remote", "--tags", "--refs", remote, f"refs/tags/{tag}", check=False)
        return listing.returncode == 0 and bool(listing.stdout.strip())
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", nargs="?", default=".", help="the package folder, at the repository root")
    parser.add_argument("--remote", default="origin", help="the remote for the tag (default: origin)")
    parser.add_argument("--push", action="store_true", help="push the tag to the remote")
    parser.add_argument("--message", help="the tag message (default: the add-on name and version)")
    parser.add_argument("--output", help="append version=, tag= and released= lines to this file (GITHUB_OUTPUT)")
    args = parser.parse_args()

    root = Path(args.package).resolve()
    top = Path(git(root, "rev-parse", "--show-toplevel").stdout.strip()).resolve()
    if top != root:
        sys.exit("release: the package must be at the root of its repository; the app reads the tagged tree from the root.")
    manifest = json.loads((root / "addon.json").read_text(encoding="utf-8"))
    version, name = manifest.get("version", ""), manifest.get("name", manifest.get("id", "add-on"))
    tag = f"v{version}"
    remote = args.remote if args.push else None

    def output(released):
        if args.output:
            with open(args.output, "a", encoding="utf-8") as stream:
                stream.write(f"version={version}\ntag={tag}\nreleased={'true' if released else 'false'}\n")

    if tag_exists(root, tag, remote):
        print(f"release: {tag} exists; nothing to release. Change version in addon.json for a new release.")
        output(False)
        return 0
    tracked_changes = git(root, "status", "--porcelain", "--untracked-files=no").stdout.strip()
    if tracked_changes:
        sys.exit("release: the checkout has changes to tracked files; commit them first.")

    declared = [path for path in manifest.get("files", []) if isinstance(path, str)]
    missing = [path for path in declared if not (root / path).is_file()]
    if missing:
        sys.exit(f"release: declared files are missing (build first): {', '.join(missing)}")
    tracked = set(git(root, "ls-files", "-z").stdout.split("\0"))
    built = [path for path in declared if path not in tracked]

    head = git(root, "rev-parse", "HEAD").stdout.strip()
    commit = head
    if built:
        # A temporary index: the user's index and branch stay as they are.
        index = Path(git(root, "rev-parse", "--path-format=absolute", "--git-path", "hakone-release-index").stdout.strip())
        env = {**os.environ, "GIT_INDEX_FILE": str(index)}
        try:
            git(root, "read-tree", "HEAD", env=env)
            git(root, "add", "--force", "--", *built, env=env)
            tree = git(root, "write-tree", env=env).stdout.strip()
        finally:
            index.unlink(missing_ok=True)
        message = f"Release {tag}\n\nBuilt files: {', '.join(built)}\nSource: {head}\n"
        commit = git(root, "commit-tree", tree, "-p", head, "-m", message).stdout.strip()
    git(root, "tag", "-a", tag, commit, "-m", args.message or f"{name} {version}")
    print(f"release: tagged {tag} at {commit[:12]} (source {head[:12]}; built files: {', '.join(built) or 'none'}).")
    if args.push:
        git(root, "push", args.remote, f"refs/tags/{tag}", capture=False)
    output(True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
