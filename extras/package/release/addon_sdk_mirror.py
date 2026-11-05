#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Publish contrib-addons/sdk to the hakone-org/addon-sdk mirror.

The SDK lives in this repository: the host code, the schemas and the docs
change together. Add-on authors use the mirror, which has the SDK folder
at its root, so its reusable workflows and composite actions are where
GitHub looks for them.

Each run makes one commit on the mirror's master branch whose tree is the
SDK folder at the source commit, with the previous mirror head as parent.
The mirror history is a log of syncs; the source commit is in each message.
Nothing is pushed when the tree did not change.

With --release-tag sdk-vX.Y.Z (a tag of this repository), the mirror also
gets the annotated tag vX.Y.Z and the moving tag vX. VERSION in the SDK
must say X.Y.Z. With --no-master (a release tag of a commit that is not the
branch tip), the tag gets its own commit and master does not move.
"""
import argparse
import re
import subprocess
import sys

PREFIX = "contrib-addons/sdk"
MIRROR_REF = "refs/hakone/addon-sdk-mirror"


def git(*args, check=True, input=None):
    result = subprocess.run(["git", *args], capture_output=True, text=True, input=input, check=False)
    if check and result.returncode != 0:
        raise SystemExit(f"addon-sdk-mirror: git {' '.join(args)} failed: {result.stderr.strip()}")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", default="HEAD", help="the source commit (default: HEAD)")
    parser.add_argument("--remote", required=True, help="the mirror repository URL")
    parser.add_argument("--release-tag", help="a source tag sdk-vX.Y.Z to publish as vX.Y.Z and vX")
    parser.add_argument("--source-name", default="hakone-org/hakone", help="the source repository name for messages")
    parser.add_argument("--no-master", dest="master", action="store_false",
                        help="do not move the mirror's master branch (a release tag of an older source commit)")
    parser.add_argument("--push", action="store_true", help="push to the mirror; without it, only print the plan")
    args = parser.parse_args(argv)

    source = git("rev-parse", "--verify", f"{args.source}^{{commit}}").stdout.strip()
    tree = git("rev-parse", "--verify", f"{source}:{PREFIX}").stdout.strip()
    version = git("cat-file", "blob", f"{source}:{PREFIX}/VERSION").stdout.strip()
    subject = git("log", "-1", "--format=%s", source).stdout.strip()

    release = None
    if args.release_tag:
        match = re.fullmatch(r"sdk-v(\d+)\.(\d+)\.(\d+)", args.release_tag)
        if not match:
            raise SystemExit(f"addon-sdk-mirror: {args.release_tag} is not a tag sdk-vX.Y.Z.")
        release = f"{match[1]}.{match[2]}.{match[3]}"
        if release != version:
            raise SystemExit(f"addon-sdk-mirror: {args.release_tag} does not agree with {PREFIX}/VERSION ({version}).")

    # The mirror head, if the mirror has a master branch yet.
    head = None
    if git("ls-remote", "--exit-code", "--heads", args.remote, "master", check=False).returncode == 0:
        git("fetch", "--quiet", "--depth=1", "--no-tags", args.remote, f"+refs/heads/master:{MIRROR_REF}")
        head = git("rev-parse", MIRROR_REF).stdout.strip()
    head_tree = git("rev-parse", f"{head}^{{tree}}").stdout.strip() if head else None

    def sync_commit():
        message = (f"Sync {PREFIX} from {args.source_name}@{source[:12]}\n\n{subject}\n\n"
                   f"Source: https://github.com/{args.source_name}/commit/{source}\n")
        parents = ["-p", head] if head else []
        return git("commit-tree", tree, *parents, input=message).stdout.strip()

    pushes = []
    if head_tree == tree:
        commit = head
        print(f"addon-sdk-mirror: master has this SDK tree already ({tree[:12]}).")
    else:
        commit = sync_commit()
        if args.master:
            pushes.append(f"{commit}:refs/heads/master")
        print(f"addon-sdk-mirror: {commit[:12]} has the SDK at {source[:12]}.")

    if release:
        tag, major = f"v{release}", f"v{release.split('.')[0]}"
        existing = git("ls-remote", "--tags", args.remote, f"refs/tags/{tag}^{{}}", check=False).stdout.split()
        if existing:
            git("fetch", "--quiet", "--depth=1", args.remote, f"+refs/tags/{tag}:refs/hakone/addon-sdk-tag")
            if git("rev-parse", "refs/hakone/addon-sdk-tag^{tree}").stdout.strip() != tree:
                raise SystemExit(f"addon-sdk-mirror: the mirror has {tag} with other files; a release tag never moves.")
            print(f"addon-sdk-mirror: the mirror has {tag} already.")
        else:
            tag_object = git("mktag", input=(
                f"object {commit}\ntype commit\ntag {tag}\n"
                f"tagger {git('var', 'GIT_COMMITTER_IDENT').stdout.strip()}\n\n"
                f"Hakone add-on SDK {release}\n\nSource: {args.source_name} {args.release_tag}\n")).stdout.strip()
            pushes.append(f"{tag_object}:refs/tags/{tag}")
        # The moving major tag: add-ons that use @vX get each compatible release.
        pushes.append(f"+{commit}:refs/tags/{major}")

    if not pushes:
        print("addon-sdk-mirror: nothing to push.")
        return 0
    for refspec in pushes:
        print(f"addon-sdk-mirror: push {refspec}")
    if args.push:
        git("push", "--quiet", args.remote, *pushes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
