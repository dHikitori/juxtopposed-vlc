#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a metadata consumer test from the normal Meson Qt objects in WSL."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def output(arguments, directory=None):
    return subprocess.check_output(arguments, cwd=directory, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="The normal Meson build directory.")
    parser.add_argument("--output-dir", type=Path, required=True, help="The test output directory.")
    parser.add_argument("--target", default="modules/libqt_plugin.so", help="The normal Qt module target.")
    suite = parser.add_mutually_exclusive_group()
    suite.add_argument("--probe", action="store_true", help="Build the optional live probe instead of the parity suite.")
    suite.add_argument("--artwork", action="store_true", help="Build the native artwork consumer suite.")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    build = args.build_dir.resolve()
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, LC_ALL="C")
    reserve = int(7.5 * 1024**3)
    estimate = (build / args.target).stat().st_size + 16 * 1024**2
    if shutil.disk_usage(destination).free < reserve + estimate:
        raise RuntimeError("The free disk space is not sufficient for this test link.")
    # A dry run cannot resolve the always-stale generators' restat results.
    subprocess.run(["ninja", args.target, "-j2", "-l3"], cwd=build, env=environment, check=True)
    if shutil.disk_usage(destination).free < reserve + estimate:
        raise RuntimeError("The free disk space is not sufficient for this test link.")
    name = "addon_episode_probe" if args.probe else "test_addon_artwork_consumers" if args.artwork else "test_addon_episode_metadata"
    source = root / "modules/gui/qt/tests" / (name + ".cpp")
    commands = json.loads((build / "compile_commands.json").read_text())
    episode = next(item for item in commands if item["file"].endswith("/player/episode_metadata.cpp"))
    original = shlex.split(episode["command"])
    compile_args = [original[0], "-I" + str(destination), "-I" + str(root)]
    if not args.probe:
        moc = shutil.which("moc")
        if not moc:
            raise RuntimeError("Add the Qt moc tool to PATH before this test.")
        subprocess.run([moc, str(source), "-o", str(destination / (name + ".moc"))], check=True)
        compile_args += shlex.split(output(["pkg-config", "--cflags", "Qt6Test"]))
    index = 1
    while index < len(original):
        value = original[index]
        if value in ("-MD", "-MMD"):
            index += 1
            continue
        if value in ("-MQ", "-MF"):
            index += 2
            continue
        if value == "-o":
            compile_args += [value, str(destination / (name + ".o"))]
            index += 2
            continue
        if value == "-c":
            compile_args += [value, str(source)]
            index += 2
            continue
        compile_args.append(value)
        index += 1
    subprocess.run(compile_args, cwd=build, check=True)
    graph = json.loads(output(["ninja", "-t", "compdb", "-x"], build))
    link = next(item for item in graph if item.get("output") == args.target)
    original = shlex.split(link["command"])
    link_args = []
    index = 0
    while index < len(original):
        value = original[index]
        if value == "-o":
            link_args += [value, str(destination / name)]
            index += 2
            continue
        if value == "-shared" or value.startswith(("-Wl,--version-script", "-Wl,-soname")):
            index += 1
            continue
        link_args.append(value)
        index += 1
    link_args += [str(destination / (name + ".o")), "-no-pie"]
    if not args.probe:
        link_args += shlex.split(output(["pkg-config", "--libs", "Qt6Test"]))
    subprocess.run(link_args, cwd=build, check=True)
    print(destination / name)


if __name__ == "__main__":
    main()
