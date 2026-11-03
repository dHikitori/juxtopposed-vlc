#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Link the library target tests with the normal Qt module objects in WSL."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def capture(arguments, directory=None):
    return subprocess.check_output(arguments, cwd=directory, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--min-free-bytes", type=int, default=0)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    build = args.build_dir.resolve()
    destination = args.output_dir.resolve()
    target = "modules/libqt_plugin.so"
    module = build / target
    sources = [root / "modules/gui/qt/addons" / (name + extension)
               for name in ("addon_native_library_store", "addon_native_library_targets", "addon_discovery_service", "addon_player_discovery")
               for extension in (".cpp", ".hpp")]
    if not module.is_file() or any(path.stat().st_mtime_ns > module.stat().st_mtime_ns for path in sources):
        raise RuntimeError("Build the current normal Qt module before this test link.")
    destination.mkdir(parents=True, exist_ok=True)
    estimate = module.stat().st_size + 16 * 1024**2
    if args.min_free_bytes and shutil.disk_usage(destination).free < args.min_free_bytes + estimate:
        raise RuntimeError("The free disk space is below the test link reserve.")
    name = "test_addon_native_library_store"
    source = root / "modules/gui/qt/tests" / (name + ".cpp")
    commands = json.loads((build / "compile_commands.json").read_text())
    command = next(row for row in commands if row["file"].endswith("/addons/addon_native_library_store.cpp"))
    original = shlex.split(command["command"])
    moc = shutil.which("moc")
    if not moc:
        raise RuntimeError("Add the Qt moc tool to PATH before this test.")
    subprocess.run([moc, "-DVLC_ADDON_REAL_ML_DISPATCHER", str(source), "-o", str(destination / (name + ".moc"))], check=True)
    compile_args = [original[0], "-I" + str(destination), "-I" + str(root), "-DVLC_ADDON_REAL_ML_DISPATCHER",
                    '-DVLC_ADDON_NATIVE_BUILD_ROOT="' + str(build) + '"']
    compile_args += shlex.split(capture(["pkg-config", "--cflags", "Qt6Test", "sqlite3"]))
    index = 1
    while index < len(original):
        value = original[index]
        if value in ("-MD", "-MMD"):
            index += 1
        elif value in ("-MQ", "-MF"):
            index += 2
        elif value == "-o":
            compile_args += [value, str(destination / (name + ".o"))]
            index += 2
        elif value == "-c":
            compile_args += [value, str(source)]
            index += 2
        else:
            compile_args.append(value)
            index += 1
    subprocess.run(compile_args, cwd=build, check=True)
    graph = json.loads(capture(["ninja", "-t", "compdb", "-x"], build))
    original = shlex.split(next(row["command"] for row in graph if row.get("output") == target))
    link_args = []
    index = 0
    while index < len(original):
        value = original[index]
        if value == "-o":
            link_args += [value, str(destination / name)]
            index += 2
        elif value == "-shared" or value.startswith(("-Wl,--version-script", "-Wl,-soname")):
            index += 1
        else:
            link_args.append(value)
            index += 1
    link_args += [str(destination / (name + ".o")), str(build / "lib/libvlc.so"), "-no-pie"]
    link_args += shlex.split(capture(["pkg-config", "--libs", "Qt6Test", "sqlite3"]))
    if args.min_free_bytes and shutil.disk_usage(destination).free < args.min_free_bytes + estimate:
        raise RuntimeError("The free disk space is below the test link reserve.")
    subprocess.run(link_args, cwd=build, check=True)
    print(destination / name)


if __name__ == "__main__":
    main()
