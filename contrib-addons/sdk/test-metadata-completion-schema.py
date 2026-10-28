#!/usr/bin/env python3
"""Validate the scoped metadata completion policy schema and its example."""
import copy
import json
from pathlib import Path
try:
    from jsonschema import Draft202012Validator
except ImportError as error:
    raise SystemExit("Install jsonschema 4.23.0 in the SDK tool environment.") from error

def main():
    root = Path(__file__).resolve().parent
    schema = json.loads((root / "metadata-completion-policy.schema.json").read_text())
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    example = json.loads((root / "examples/metadata-completion-contribution.json").read_text())
    base = example["completionPolicy"]
    cases = [("contribution example", base, True)]
    def case(name, value, valid=False): cases.append((name, value, valid))
    def rule(**values): return dict(base["httpAbsent"][0], **values)
    def policy(**values): return dict(base, **values)
    for status in [404, 410]:
        case("one permitted status " + str(status), policy(httpAbsent=[rule(statuses=[status])]), True)
    case("rule count16", policy(httpAbsent=[rule(feature="feature." + str(i)) for i in range(16)]), True)
    for field in base:
        value=copy.deepcopy(base); del value[field]; case("missing " + field, value)
    for value in [None, [], "policy", True]: case("invalid object " + repr(value), value)
    for version in [0, 2, True, "1", None]: case("invalid version " + repr(version), policy(version=version))
    case("extra policy field", policy(complete=True))
    for rules in [[], {}, None, [None], ["rule"], [rule()] * 2,
                  [rule(feature="feature." + str(i)) for i in range(17)]]:
        case("invalid rules " + repr(rules), policy(httpAbsent=rules))
    for statuses in [[], [404, 410, 500], [404, 404], [401], [429], [500], ["404"], [True], None]:
        case("invalid statuses " + repr(statuses), policy(httpAbsent=[rule(statuses=statuses)]))
    for name in ["feature", "operation"]:
        for value in ["", "x" * 129, "bad\nname", "bad\u0085name", False]:
            case("invalid " + name + " " + repr(value), policy(httpAbsent=[rule(**{name:value})]))
    case("extra rule field", policy(httpAbsent=[rule(complete=True)]))
    for name, value, valid in cases:
        if validator.is_valid(value) != valid:
            raise SystemExit("Metadata completion schema case failed: " + name)
    print(json.dumps({"draft":"2020-12", "cases":len(cases), "failed":0,
                      "scope":"completionPolicy shape only; native manifest validation remains required"}))
if __name__ == "__main__": main()
