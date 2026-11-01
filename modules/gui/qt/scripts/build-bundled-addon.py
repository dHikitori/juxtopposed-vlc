#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a bundled add-on and stage its package files, or install staged packages."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

TARGET = "wasm32-unknown-unknown"


def declared_files(package):
    manifest = json.loads((package / "addon.json").read_text(encoding="utf-8"))
    files = manifest.get("files", [])
    entry = manifest.get("runtime", {}).get("entry", "")
    if not isinstance(files, list) or not all(isinstance(name, str) for name in files) or not entry:
        raise ValueError(f"{package / 'addon.json'} needs a file list and a runtime entry.")
    for name in [entry, *files]:
        parts = Path(name).parts
        if Path(name).is_absolute() or ".." in parts or not parts:
            raise ValueError(f"{package / 'addon.json'} declares an unsafe path: {name}")
    return entry, ["addon.json", *(name for name in files if name not in ("addon.json", entry))]


def build_module(args, package):
    cargo = shutil.which(args.cargo)
    if not cargo:
        raise ValueError("Cargo is not available. Install Rust and Cargo, or disable addon-runtime.")
    command = [cargo, "build", "--locked", "--release", "--target", TARGET,
               "--manifest-path", str(package / "Cargo.toml"), "--target-dir", str(args.target_dir),
               "--jobs", str(max(1, args.jobs)), "--message-format=json-render-diagnostics"]
    if args.offline:
        command.append("--offline")
    environment = os.environ.copy()
    environment.setdefault("RUSTUP_TOOLCHAIN", "stable")
    environment["RUSTUP_AUTO_INSTALL"] = "0"
    # Panic locations name source files. Keep the builder's folders out of
    # the module, so that every machine makes the same bytes.
    cargo_home = Path(environment.get("CARGO_HOME") or Path.home() / ".cargo").resolve()
    remaps = [f"--remap-path-prefix={cargo_home}=/cargo", f"--remap-path-prefix={package.parent.resolve()}=/contrib-addons"]
    environment["RUSTFLAGS"] = " ".join(filter(None, [environment.get("RUSTFLAGS", ""), *remaps]))
    result = subprocess.run(command, cwd=package, env=environment, stdout=subprocess.PIPE, text=True, check=False)
    if result.returncode:
        print(f"The {package.name} add-on build did not complete. If the error says that the {TARGET} "
              f"target is not installed, run: rustup target add {TARGET}", file=sys.stderr)
        raise SystemExit(result.returncode)
    manifest_path = str((package / "Cargo.toml").resolve())
    for line in result.stdout.splitlines():
        message = json.loads(line) if line.startswith("{") else {}
        if message.get("reason") == "compiler-artifact" and message.get("manifest_path") == manifest_path:
            modules = [Path(name) for name in message.get("filenames", []) if name.endswith(".wasm")]
            if modules:
                return modules[0]
    raise ValueError(f"Cargo did not create a WebAssembly module for {package.name}.")


def write_if_changed(path, content):
    if path.is_file() and path.read_bytes() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name("." + path.name + ".next")
    temporary.write_bytes(content)
    os.replace(temporary, path)


def stage(args):
    package = args.package.resolve()
    entry, files = declared_files(package)
    module = build_module(args, package)
    output = args.output.resolve()
    contents = {entry: module.read_bytes()}
    for name in files:
        source = package / name
        if not source.is_file():
            raise ValueError(f"{package.name} declares {name}, but the file is missing.")
        contents[name] = source.read_bytes()
    for name, content in contents.items():
        write_if_changed(output / name, content)
    # Remove files that an earlier manifest declared.
    for path in sorted(output.rglob("*"), reverse=True):
        relative = path.relative_to(output).as_posix()
        if path.is_file() and relative not in contents:
            path.unlink()
        elif path.is_dir() and not any(path.iterdir()):
            path.rmdir()
    if args.depfile:
        # Cargo lists the module's Rust sources and embedded files.
        cargo_depfile = module.with_suffix(".d")
        inputs = [str(package / name) for name in files]
        if cargo_depfile.is_file():
            inputs += cargo_depfile.read_text(encoding="utf-8").split(":", 1)[1].replace("\\\n", " ").split()
        escaped = " ".join(path.replace(" ", "\\ ") for path in dict.fromkeys(inputs))
        args.depfile.write_text(f"{args.stamp}: {escaped}\n", encoding="utf-8")
    if args.stamp:
        args.stamp.write_text(module.name + "\n", encoding="utf-8")


def install(args):
    prefix = os.environ.get("MESON_INSTALL_DESTDIR_PREFIX")
    destination = Path(prefix) / args.destination if prefix else Path(args.destination)
    for name in args.names:
        target = destination / name
        shutil.rmtree(target, ignore_errors=True)
        shutil.copytree(args.staged / name, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build the module and stage the package")
    build.add_argument("--cargo", default="cargo")
    build.add_argument("--package", required=True, type=Path)
    build.add_argument("--output", required=True, type=Path)
    build.add_argument("--target-dir", required=True, type=Path)
    build.add_argument("--stamp", type=Path)
    build.add_argument("--depfile", type=Path)
    build.add_argument("--offline", action="store_true")
    build.add_argument("--jobs", type=int, default=2)
    copy = commands.add_parser("install", help="install staged packages")
    copy.add_argument("--staged", required=True, type=Path)
    copy.add_argument("--destination", required=True)
    copy.add_argument("names", nargs="+")
    args = parser.parse_args()
    try:
        stage(args) if args.command == "build" else install(args)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
