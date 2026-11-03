#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the shared corpus against the published schemas (Draft 2020-12).

Each corpus/<kind>/*.json case is {"description", "valid", "value",
"context"?}; <kind>.schema.json must accept exactly the valid cases. The host
runs the same cases in test_qt_addon_schema_corpus. Use jsonschema 4.23.0 in
a separate tool environment.
"""
import json
import sys
from pathlib import Path

try:
    from jsonschema import Draft202012Validator, FormatChecker
except ImportError:
    # 77 tells a test runner to skip: the tool environment is not present.
    print("SKIP: install jsonschema 4.23.0 to run this check.", file=sys.stderr)
    sys.exit(77)


def main():
    root = Path(__file__).resolve().parent
    failures, cases, kinds = [], 0, 0
    for directory in sorted(path for path in (root / "corpus").iterdir() if path.is_dir()):
        schema_path = root / (directory.name + ".schema.json")
        if not schema_path.is_file():
            failures.append(f"{directory.name}: no {schema_path.name}")
            continue
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema, format_checker=FormatChecker())
        kinds += 1
        valid_cases = invalid_cases = 0
        for case_path in sorted(directory.glob("*.json")):
            case = json.loads(case_path.read_text(encoding="utf-8"))
            cases += 1
            accepted = validator.is_valid(case["value"])
            if case["valid"]:
                valid_cases += 1
            else:
                invalid_cases += 1
            if accepted != case["valid"]:
                errors = [] if accepted else [error.message for error in validator.iter_errors(case["value"])][:2]
                failures.append(f"{directory.name}/{case_path.name}: schema "
                                f"{'accepts' if accepted else 'refuses'} ({case.get('description', '')}) {errors}")
        if valid_cases == 0 or invalid_cases == 0:
            failures.append(f"{directory.name}: needs valid and invalid cases")
    print(json.dumps({"draft": "2020-12", "schemas": kinds, "cases": cases, "failed": len(failures)}))
    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
