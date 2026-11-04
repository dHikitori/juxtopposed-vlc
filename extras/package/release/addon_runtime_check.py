# SPDX-License-Identifier: GPL-2.0-or-later
"""Do a release worker test without API keys, network requests or Qt."""
import json
import os
from pathlib import Path
import re
import subprocess


def installation_files(executable):
    executable = Path(executable).resolve()
    if executable.name == "AppRun":
        binary_directory = executable.parent / "usr/bin"
        data_directory = executable.parent / "usr/share/vlc"
    elif executable.parent.name == "MacOS" and executable.parent.parent.name == "Contents":
        binary_directory = executable.parent
        data_directory = executable.parent.parent / "Resources/share"
    elif executable.suffix.lower() == ".exe":
        binary_directory = executable.parent
        data_directory = executable.parent / "share/vlc"
    else:
        binary_directory = executable.parent
        data_directory = executable.parent.parent / "share/vlc"
    suffix = ".exe" if executable.suffix.lower() == ".exe" else ""
    return (binary_directory / ("hakone-addon-worker" + suffix),
            data_directory / "addons/reference/thesubtitledb/dist/addon.wasm")


def check_worker(worker, module):
    worker, module = Path(worker).resolve(), Path(module).resolve()
    if not worker.is_file() or not module.is_file():
        raise RuntimeError("The release worker or reference add-on is missing.")
    request = {
        "type": "invoke", "id": "release-worker-smoke", "contribution": "search",
        "operation": "searchAssets", "context": {
            "query": "Release worker check",
            "identity": {"kind": "movie", "title": "Release worker check"}
        },
        "settings": {}, "credentials": {"apiKey": {"present": False}}
    }
    # Keep the build machine's secrets and loader paths out of the worker.
    # The test must find all dependencies in the release.
    environment = {}
    if os.name == "nt" and "SystemRoot" in os.environ:
        environment["SystemRoot"] = os.environ["SystemRoot"]
    try:
        done = subprocess.run([str(worker), str(module)], input=json.dumps(request) + "\n",
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                              timeout=20, check=True, env=environment)
    except (OSError, subprocess.SubprocessError) as error:
        diagnostic = getattr(error, "stderr", "") or ""
        if isinstance(diagnostic, bytes):
            diagnostic = diagnostic.decode("utf-8", errors="replace")
        diagnostic = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", "?", diagnostic).strip()[-1024:]
        suffix = f" Worker diagnostic: {diagnostic}" if diagnostic else ""
        raise RuntimeError("The release worker did not complete the test." + suffix) from error
    if len(done.stdout) > 2 * 1024 * 1024:
        raise RuntimeError("The release worker returned too much data.")
    try:
        messages = [json.loads(line) for line in done.stdout.splitlines()]
        if not messages or not isinstance(messages[0], dict) or messages[0].get("type") != "ready" or messages[0].get("protocol") != "wasm-message-v1":
            raise ValueError("The worker handshake is not correct.")
        results = []
        for batch in messages[1:]:
            if batch.get("type") != "messages" or not isinstance(batch.get("messages"), list):
                raise ValueError("The worker message envelope is not correct.")
            for message in batch["messages"]:
                if message.get("type") != "result":
                    raise ValueError("The worker requested a host operation without an API key.")
                results.append(message)
        if (len(results) != 1 or results[0].get("id") != request["id"]
                or results[0].get("status") != "auth_required" or not isinstance(results[0].get("data"), dict)):
            raise ValueError("The reference add-on did not request an API key.")
    except (ValueError, TypeError, AttributeError) as error:
        raise RuntimeError("The worker protocol or API key check did not pass.") from error
    return "The worker and reference add-on passed the test without an API key."


def verify_installation(executable):
    return check_worker(*installation_files(executable))
