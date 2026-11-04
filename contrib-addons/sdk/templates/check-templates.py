#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build each author template and check it as an install would.

  check-templates.py --validator <vlc-addon-validate> [--ui <AddonUi.js>]
                     [--worker <hakone-addon-worker>]

Each template is copied to a temporary folder. A Rust template runs its
unit tests, builds for wasm32-unknown-unknown (offline) and gets its
dist/addon.wasm. Then vlc-addon-validate checks the package. With --worker,
a template that declares runtime.selfTest must start in the real worker and
pass its self-test. Exit status 77 means the Rust toolchain is missing.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SDK = ROOT.parent


def self_test(worker, module):
    process = subprocess.Popen([worker, str(module)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True)
    try:
        ready = json.loads(process.stdout.readline() or "{}")
        if ready.get("type") != "ready":
            return "the module did not start"
        process.stdin.write(json.dumps({"type": "self-test", "id": "hakone-self-test"}) + "\n")
        process.stdin.flush()
        reply = json.loads(process.stdout.readline() or "{}")
        results = [m for m in reply.get("messages", []) if m.get("type") == "result"]
        if not results or results[0].get("status") != "ok":
            return "the self-test did not pass: " + json.dumps(reply)[:200]
        return ""
    finally:
        process.stdin.close()
        process.kill()
        process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True)
    parser.add_argument("--ui")
    parser.add_argument("--worker")
    arguments = parser.parse_args()
    cargo = shutil.which("cargo")
    failures = []
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        # The templates name the SDK as ../../message-v1.
        shutil.copytree(SDK / "message-v1", work / "message-v1", ignore=shutil.ignore_patterns("target"))
        folders = []
        for template in sorted(path for path in ROOT.iterdir() if (path / "addon.json").is_file()):
            folder = work / "templates" / template.name
            shutil.copytree(template, folder, ignore=shutil.ignore_patterns("target", "dist"))
            if (folder / "Cargo.toml").is_file():
                if not cargo:
                    print("SKIP: cargo is not installed", file=sys.stderr)
                    return 77
                environment = dict(os.environ, CARGO_TARGET_DIR=str(work / "target" / template.name),
                                   RUSTUP_AUTO_INSTALL="0")
                for command in (["cargo", "test", "--locked", "--offline", "--quiet"],
                                ["cargo", "build", "--locked", "--offline", "--release", "--target", "wasm32-unknown-unknown"]):
                    run = subprocess.run(command, cwd=folder, env=environment, capture_output=True, text=True)
                    if run.returncode:
                        if "wasm32-unknown-unknown" in run.stderr and "target may not be installed" in run.stderr:
                            print("SKIP: the wasm32-unknown-unknown target is not installed", file=sys.stderr)
                            return 77
                        failures.append(f"{template.name}: {' '.join(command[:2])} failed\n{run.stderr[-2000:]}")
                        break
                else:
                    crate = json.loads(subprocess.run(["cargo", "metadata", "--no-deps", "--format-version", "1", "--offline"],
                                                      cwd=folder, env=environment, capture_output=True, text=True).stdout)
                    name = crate["packages"][0]["name"].replace("-", "_")
                    (folder / "dist").mkdir(exist_ok=True)
                    shutil.copy(work / "target" / template.name / "wasm32-unknown-unknown" / "release" / f"{name}.wasm",
                                folder / "dist" / "addon.wasm")
                    manifest = json.loads((folder / "addon.json").read_text(encoding="utf-8"))
                    if arguments.worker and manifest.get("runtime", {}).get("selfTest"):
                        error = self_test(arguments.worker, folder / "dist" / "addon.wasm")
                        if error:
                            failures.append(f"{template.name}: {error}")
            folders.append(str(folder))
        command = [arguments.validator] + (["--ui", arguments.ui] if arguments.ui else []) + folders
        run = subprocess.run(command, capture_output=True, text=True)
        print(run.stdout, end="")
        if run.returncode:
            failures.append("vlc-addon-validate refused a template")
    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
