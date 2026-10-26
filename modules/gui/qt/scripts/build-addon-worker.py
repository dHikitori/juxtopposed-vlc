#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the application worker. Do not use this script for add-on installs."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def target_triple(system, cpu, abi):
    cpu = {"amd64": "x86_64", "arm64": "aarch64"}.get(cpu, cpu)
    if cpu not in ("x86_64", "aarch64"):
        raise ValueError("The add-on worker operates on x86_64 and aarch64 hosts.")
    if system.startswith(("mingw", "windows")):
        return f"{cpu}-pc-windows-{'msvc' if abi == 'msvc' else 'gnu'}"
    if system.startswith(("darwin", "macos")):
        return f"{cpu}-apple-darwin"
    if system.startswith("linux"):
        return f"{cpu}-unknown-linux-{'musl' if abi == 'musl' else 'gnu'}"
    raise ValueError(f"The add-on worker has no target for {system}/{cpu}.")


def check_free_space(paths, minimum, reserve):
    if minimum < 0 or reserve < 0:
        raise ValueError("The free-space limit and build reserve must be zero or larger.")
    if minimum == 0 and reserve == 0:
        return
    for path in paths:
        directory = Path(path).resolve()
        while not directory.exists():
            directory = directory.parent
        free = shutil.disk_usage(directory).free
        if free < minimum + reserve:
            raise ValueError(f"The worker build cannot start. At least {minimum + reserve} free bytes "
                             f"are necessary at {directory}. Only {free} bytes are available.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target-dir", type=Path)
    parser.add_argument("--target", default="")
    parser.add_argument("--system", default=sys.platform)
    parser.add_argument("--cpu", default="")
    parser.add_argument("--abi", default="gnu")
    parser.add_argument("--cross", action="store_true")
    parser.add_argument("--linker", default="")
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    manifest = args.manifest.resolve()
    if manifest.name != "Cargo.toml" or not manifest.is_file() or not manifest.with_name("Cargo.lock").is_file():
        parser.error("The worker Cargo.toml or Cargo.lock file is missing.")
    cargo = shutil.which(args.cargo)
    if not cargo:
        parser.error("Cargo is not available. Install Rust and Cargo, or disable addon-runtime.")
    try:
        target = args.target or (target_triple(args.system, args.cpu, args.abi) if args.cross else "")
    except ValueError as error:
        parser.error(str(error))
    if target and (not all(c.isalnum() or c in "-_." for c in target) or len(target) > 128):
        parser.error("The Rust target triple is not correct.")
    output = args.output.resolve()
    target_dir = (args.target_dir or output.parent / "addon-worker-cargo").resolve()
    try:
        minimum = int(os.environ.get("VLC_ADDON_MIN_FREE_BYTES", "0"))
        reserve = int(os.environ.get("VLC_ADDON_BUILD_RESERVE_BYTES", "0"))
        check_free_space((target_dir, output.parent), minimum, reserve)
    except (OSError, ValueError) as error:
        parser.error(f"The worker free-space check did not pass. {error}")
    command = [cargo, "build", "--locked", "--release", "--manifest-path", str(manifest),
               "--target-dir", str(target_dir), "--jobs", str(max(1, args.jobs))]
    if target:
        command += ["--target", target]
    if args.offline:
        command.append("--offline")
    environment = os.environ.copy()
    # The worker uses stable Rust. Keep an explicit toolchain override.
    environment.setdefault("RUSTUP_TOOLCHAIN", "stable")
    environment["RUSTUP_AUTO_INSTALL"] = "0"
    if target and args.linker:
        environment[f"CARGO_TARGET_{target.upper().replace('-', '_')}_LINKER"] = args.linker
    # Build the VLC source with its Cargo.lock file.
    # Do not install toolchains or targets in this script.
    result = subprocess.run(command, cwd=manifest.parent, env=environment, check=False)
    if result.returncode:
        print("The worker build did not complete. Examine the Rust, target and linker errors above. "
              "Set addon-runtime=disabled to omit executable add-ons.",
              file=sys.stderr)
        return result.returncode
    suffix = ".exe" if "windows" in target or (not target and os.name == "nt") else ""
    built = target_dir / target / "release" / ("vlc-addon-worker" + suffix)
    if not built.is_file():
        print("Cargo did not create the add-on worker program.", file=sys.stderr)
        return 1
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".addon-worker-", delete=False) as file:
            temporary = Path(file.name)
        shutil.copyfile(built, temporary)
        temporary.chmod(0o755)
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
