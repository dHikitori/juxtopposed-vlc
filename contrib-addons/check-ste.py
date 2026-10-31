#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Report add-on app text that breaks the mechanical ASD-STE100 rules.

The check reads the translatable strings (qsTr, tr, qtr) of the add-on host
and its screens and reports:

  long       a sentence with more than 20 words (procedural limit; 25 for
             descriptive text is allowed with --descriptive)
  ing        a word that ends in -ing used as a noun or adjective
             (an approved word list keeps "string", "setting", ...)
  passive    a "be" verb followed by a past participle
  contract   a contraction ("can't", "don't", ...)
  word       an unapproved word with a simpler approved alternative

It is a screening tool: a person decides each finding. Use --json for a
machine report. Exit status is 1 when findings remain and --strict is set.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    "modules/gui/qt/addons/*.cpp", "modules/gui/qt/addons/*.hpp",
    "modules/gui/qt/player/qml/redesign/*Addon*.qml",
    "modules/gui/qt/player/qml/redesign/RedesignAccountSetupDialog.qml",
    "modules/gui/qt/player/qml/redesign/RedesignIdentityDialog.qml",
    "modules/gui/qt/player/qml/redesign/AddonUi.js",
    "modules/gui/qt/maininterface/addon_guest_sources.cpp",
]
STRING = re.compile(r'\b(?:qsTr|tr|qtr)\(\s*"((?:[^"\\]|\\.)*)"')
# -ing words that are nouns in the approved vocabulary or technical names.
ING_OK = {"string", "setting", "settings", "thing", "nothing", "something", "during", "bring", "spring", "ring",
          "king", "bring", "using", "streaming", "loading", "playing", "missing", "pending", "rating", "working",
          "existing", "following", "remaining", "matching", "waiting", "incoming", "outgoing", "running"}
# Unapproved words with an approved alternative (a short screening list).
WORDS = {"utilize": "use", "utilise": "use", "please": "(remove)", "simply": "(remove)", "just": "(remove)",
         "obtain": "get", "require": "need", "requires": "needs", "required": "necessary", "ensure": "make sure",
         "approximately": "about", "additional": "more", "attempt": "try", "commence": "start",
         "terminate": "stop", "prior": "before", "subsequent": "after", "via": "through", "currently": "now",
         "allow": "let", "allows": "lets", "however": "but", "therefore": "thus", "unable": "cannot",
         "e.g.": "for example", "i.e.": "that is", "etc.": "(list the items)"}
BE = r"(?:is|are|was|were|be|been|being)"
PASSIVE = re.compile(rf"\b{BE}\s+(?:not\s+)?(\w+ed|shown|known|given|done|made|kept|found|sent|set|seen|taken|written|read|run)\b", re.I)
CONTRACTION = re.compile(r"\b\w+n't\b|\b\w+'(?:ll|re|ve|d)\b|\bit's\b", re.I)


def strings():
    seen = {}
    for pattern in SOURCES:
        for path in sorted(ROOT.glob(pattern)):
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in STRING.finditer(text):
                value = bytes(match.group(1), "utf-8").decode("unicode_escape", errors="replace")
                line = text.count("\n", 0, match.start()) + 1
                seen.setdefault(value, f"{path.relative_to(ROOT)}:{line}")
    return seen


def findings(text, limit):
    result = []
    plain = re.sub(r"%\d+|<[^>]+>", "X", text)
    for sentence in re.split(r"(?<=[.!?])\s+", plain):
        words = re.findall(r"[A-Za-z][A-Za-z'.-]*", sentence)
        if len(words) > limit:
            result.append(("long", f"{len(words)} words"))
    for word in re.findall(r"\b[A-Za-z]+ing\b", plain):
        if word.lower() not in ING_OK:
            result.append(("ing", word))
    for match in PASSIVE.finditer(plain):
        result.append(("passive", match.group(0)))
    for match in CONTRACTION.finditer(plain):
        result.append(("contract", match.group(0)))
    for word in re.findall(r"[A-Za-z.]+", plain):
        alternative = WORDS.get(word.lower())
        if alternative:
            result.append(("word", f"{word} -> {alternative}"))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--descriptive", action="store_true", help="allow 25 words a sentence")
    parser.add_argument("--rule", action="append", help="report only these rules")
    arguments = parser.parse_args()
    limit = 25 if arguments.descriptive else 20
    report, total = [], 0
    texts = strings()
    for text, where in texts.items():
        hits = [hit for hit in findings(text, limit) if not arguments.rule or hit[0] in arguments.rule]
        total += len(hits)
        if hits:
            report.append({"where": where, "text": text, "findings": [f"{rule}: {detail}" for rule, detail in hits]})
    if arguments.json:
        print(json.dumps({"strings": len(texts), "findings": total, "items": report}, indent=2))
    else:
        for item in report:
            print(f"{item['where']}: {item['text']}")
            for finding in item["findings"]:
                print(f"    {finding}")
        print(f"{len(texts)} strings, {total} findings", file=sys.stderr)
    return 1 if arguments.strict and total else 0


if __name__ == "__main__":
    sys.exit(main())
