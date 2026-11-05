#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check an add-on package before a release, as far as a CI job can.

The app's own checks (vlc-addon-validate, the install) stay the authority.
This tool applies the published schemas and the install rules that do not
need the app: the declared files, the Git tree that an install reads from
the release tag, view documents, locale messages and panel icons.

It needs jsonschema 4.23.0 (Draft 2020-12): pip install jsonschema==4.23.0
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import unicodedata
import xml.etree.ElementTree as ElementTree

SDK = Path(__file__).resolve().parent.parent
SCHEMAS = SDK / "schemas"
MAX_FILE = 16 * 1024 * 1024
MAX_PACKAGE = 64 * 1024 * 1024
MAX_TREE = 4096
MAX_ICON = 16 * 1024
ICON_SHAPES = {"svg", "g", "path", "circle", "ellipse", "rect", "line", "polyline", "polygon"}
RESERVED = re.compile(r"^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)", re.IGNORECASE)
SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")


class Report:
    def __init__(self):
        self.errors = []

    def error(self, message):
        self.errors.append(message)


def portable(path):
    """The app's rule for a path in a package (addon_manifest.cpp)."""
    if not path or len(path) > 512 or path != unicodedata.normalize("NFC", path) or path.startswith("/"):
        return False
    if "\\" in path or ":" in path or re.search(r"[\x00-\x1f\x7f]", path):
        return False
    for part in path.split("/"):
        if (not part or part in (".", "..") or part.endswith((" ", ".")) or len(part) > 255 or RESERVED.match(part)
                or re.search(r'[<>"|?*]', part) or part.lower() == ".git"):
            return False
    return True


def load_json(path, report, what):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        report.error(f"{what} is not valid JSON: {error}")
        return None


def validator(name):
    try:
        import jsonschema
    except ImportError:
        sys.exit("check-package: install jsonschema 4.23.0 (pip install jsonschema==4.23.0).")
    schema = json.loads((SCHEMAS / name).read_text(encoding="utf-8"))
    return jsonschema.Draft202012Validator(schema)


def schema_errors(name, value, report, what):
    for error in sorted(validator(name).iter_errors(value), key=lambda error: list(error.path)):
        where = "/".join(str(part) for part in error.path) or "(root)"
        report.error(f"{what}: {where}: {error.message}")


def check_tree(root, report):
    """An install reads the tagged commit: no links, no submodules, portable
    paths that differ by more than case, at most 4096 entries."""
    try:
        listing = subprocess.run(["git", "-C", str(root), "ls-files", "-s", "-z"], check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return  # Not a Git checkout: the release job checks the tree.
    entries = [entry for entry in listing.split(b"\0") if entry]
    if len(entries) > MAX_TREE:
        report.error(f"The repository has {len(entries)} files; an install reads at most {MAX_TREE}.")
    folded = {}
    for entry in entries:
        header, _, name = entry.partition(b"\t")
        mode = header.split(b" ")[0]
        path = name.decode("utf-8", "replace")
        if mode not in (b"100644", b"100755"):
            report.error(f"{path}: links and submodules are not permitted in an add-on repository.")
        if not portable(path):
            report.error(f"{path}: the path is not portable (see the package path rules).")
        key = path.casefold()
        if key in folded:
            report.error(f"{path}: differs only by case from {folded[key]}.")
        folded[key] = path


def check_icon(path, report):
    data = path.read_bytes()
    if len(data) > MAX_ICON:
        report.error(f"{path.name}: an icon is at most {MAX_ICON} bytes.")
        return
    if b"<!ENTITY" in data or b"<!DOCTYPE" in data:
        report.error(f"{path.name}: an icon cannot declare entities.")
        return
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        report.error(f"{path.name}: the icon is not valid XML: {error}")
        return
    if root.get("viewBox") != "0 0 24 24":
        report.error(f"{path.name}: an icon needs viewBox=\"0 0 24 24\".")
    for element in root.iter():
        namespace, _, name = element.tag.rpartition("}")
        if namespace and namespace != "{http://www.w3.org/2000/svg":
            report.error(f"{path.name}: other XML namespaces are not permitted.")
        if name not in ICON_SHAPES:
            report.error(f"{path.name}: <{name}> is not permitted; use g, path, circle, ellipse, rect, line, polyline or polygon.")
        if (element.text or "").strip():
            report.error(f"{path.name}: an icon cannot contain text.")
        if any(attribute.endswith("href") or attribute == "style" for attribute in element.attrib):
            report.error(f"{path.name}: href and style attributes are not permitted.")


def messages_in(value, found):
    if isinstance(value, dict):
        if set(value) == {"message"} and isinstance(value["message"], str):
            found.add(value["message"])
        for child in value.values():
            messages_in(child, found)
    elif isinstance(value, list):
        for child in value:
            messages_in(child, found)
    return found


def check(root, tag=None, require_entry=True):
    report = Report()
    root = Path(root).resolve()
    manifest_path = root / "addon.json"
    if not manifest_path.is_file():
        report.error("addon.json is missing.")
        return report
    manifest = load_json(manifest_path, report, "addon.json")
    if not isinstance(manifest, dict):
        return report
    schema_errors("manifest.schema.json", manifest, report, "addon.json")

    version = manifest.get("version", "")
    if not isinstance(version, str) or not SEMVER.match(version):
        report.error(f"version {version!r} is not a semantic version.")
    elif tag is not None and tag != f"v{version}":
        report.error(f"The tag {tag} does not agree with version {version}: use v{version}.")

    files = manifest.get("files") if isinstance(manifest.get("files"), list) else []
    runtime = manifest.get("runtime") if isinstance(manifest.get("runtime"), dict) else {}
    entry = runtime.get("entry")
    total = 0
    for name in files:
        if not isinstance(name, str) or not portable(name):
            report.error(f"files: {name!r} is not a portable package path.")
            continue
        path = root / name
        if name == entry and not require_entry and not path.exists():
            continue
        if path.is_symlink() or not path.is_file():
            report.error(f"files: {name} is declared but is not a regular file in the package.")
            continue
        size = path.stat().st_size
        total += size
        if size > MAX_FILE:
            report.error(f"files: {name} is larger than {MAX_FILE} bytes.")
    if total > MAX_PACKAGE:
        report.error(f"The declared files total {total} bytes; the limit is {MAX_PACKAGE}.")
    if "addon.json" not in files:
        report.error("files must list addon.json.")
    if entry and entry not in files:
        report.error(f"runtime.entry {entry} is not in files.")

    # Locale files: objects of strings. The default locale must have each
    # message that a view uses.
    locales = manifest.get("locales") if isinstance(manifest.get("locales"), dict) else {}
    default_messages = None
    for code, name in (locales.get("files") or {}).items():
        path = root / name
        if not path.is_file():
            continue
        messages = load_json(path, report, name)
        if messages is None:
            continue
        if not isinstance(messages, dict) or not all(isinstance(value, str) for value in messages.values()):
            report.error(f"{name}: a locale file is an object of strings.")
            continue
        if code == locales.get("default"):
            default_messages = messages

    for contribution in manifest.get("contributions") or []:
        if not isinstance(contribution, dict):
            continue
        cid = contribution.get("id", "?")
        view = contribution.get("view")
        if isinstance(view, str) and (root / view).is_file():
            document = load_json(root / view, report, view)
            if document is not None:
                schema_errors("ui-view.schema.json", document, report, view)
                if default_messages is not None:
                    for key in sorted(messages_in(document, set()) - set(default_messages)):
                        report.error(f"{view}: the message {key!r} is not in the default locale file.")
        scene = contribution.get("scene")
        if isinstance(scene, str) and (root / scene).is_file():
            document = load_json(root / scene, report, scene)
            if document is not None:
                schema_errors("visual-scene.schema.json", document, report, scene)
        icon = contribution.get("icon")
        if isinstance(icon, str) and icon.endswith(".svg") and (root / icon).is_file():
            check_icon(root / icon, report)
        if contribution.get("type") == "ui.command" and "command" in contribution and "route" not in contribution:
            target = next((item for item in manifest.get("contributions") or []
                           if isinstance(item, dict) and item.get("id") == contribution["command"]), None)
            opens = target is not None and target.get("type") in ("ui.modal", "ui.panel")
            if not opens and "/" not in contribution["command"] and contribution["command"] not in (contribution.get("operations") or []):
                report.error(f"contribution {cid}: a worker command lists its command {contribution['command']!r} in operations.")

    settings = manifest.get("settings") if isinstance(manifest.get("settings"), dict) else {}
    if isinstance(settings.get("schema"), str) and (root / settings["schema"]).is_file():
        document = load_json(root / settings["schema"], report, settings["schema"])
        if document is not None:
            schema_errors("settings.schema.json", document, report, settings["schema"])

    check_tree(root, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", nargs="?", default=".", help="the package folder (default: the current folder)")
    parser.add_argument("--tag", help="the release tag; it must be v<version>")
    parser.add_argument("--source-only", action="store_true",
                        help="allow a missing runtime entry, before the module is built")
    args = parser.parse_args()
    report = check(args.package, args.tag, require_entry=not args.source_only)
    for message in report.errors:
        print(f"error: {message}", file=sys.stderr)
    if report.errors:
        print(f"check-package: {len(report.errors)} error(s).", file=sys.stderr)
        return 1
    print("check-package: the package passed the CI checks. The app's validator stays the authority.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
