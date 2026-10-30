#!/usr/bin/env python3
"""Check that Automake, meson and CMake register the same add-on tests.

addon-tests.txt is the reference list. Each row gives a meson test name, one
label and the build systems that register the test. The check fails when a
build file registers an add-on test that the table does not list, or when a
build file does not register a test that the table lists for it.

An add-on test is a meson test in the `addons` suite, an Automake test named
`test_qt_addon_*`, or any test in the standalone add-on CMake project. CMake
names have no `test_qt_` prefix.

With --label, print the test names that carry that label instead.
"""

import argparse
import re
import sys
from pathlib import Path

SYSTEMS = ('automake', 'meson', 'cmake')
LABELS = ('profile', 'metadata', 'source', 'ui', 'session', 'visualizer', 'app')
SELF = 'addon_test_registry'


def strip_comments(text):
    return '\n'.join(line.split('#', 1)[0] for line in text.splitlines())


def automake_tests(text):
    text = strip_comments(text.replace('\\\n', ' '))
    names = set()
    for match in re.finditer(r'^\s*(?:TESTS|check_PROGRAMS)\s*\+?=(.*)$', text, re.M):
        names.update(word for word in match.group(1).split() if word.startswith('test_'))
    return names


def closing_brace(text, start):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return index
    raise ValueError('unbalanced braces')


def meson_tests(text):
    """Return the meson tests in the `addons` suite."""
    text = strip_comments(text)
    loops = [(match.start(), match.end(), match.group(1), re.findall(r"'(\w+)'", match.group(2)))
             for match in re.finditer(r'foreach\s+(\w+)\s*:\s*\[([^\]]*)\](.*?)\bendforeach',
                                      text, re.S)]
    names = set()
    for entry in re.finditer(r'vlc_tests\s*\+=\s*\{', text):
        block = text[entry.end() - 1:closing_brace(text, entry.end() - 1) + 1]
        suite = re.search(r"'suite'\s*:\s*\[([^\]]*)\]", block)
        if not suite or "'addons'" not in suite.group(1):
            continue
        name = re.search(r"'name'\s*:\s*'(\w+)'\s*(\+\s*(\w+))?", block)
        if not name.group(2):
            names.add(name.group(1))
            continue
        for start, end, variable, items in loops:
            if start < entry.start() < end and variable == name.group(3):
                names.update(name.group(1) + item for item in items)
    for test in re.finditer(r"^\s*test\(\s*'(\w+)'(.*?)\)\s*$", text, re.M | re.S):
        if re.search(r"suite\s*:\s*\[[^\]]*'addons'", test.group(2)):
            names.add(test.group(1))
    names.discard(SELF)
    return names


def cmake_tests(text):
    text = strip_comments(text)
    names = set()
    for loop in re.finditer(r'foreach\((\w+)\s+([^)]*)\)(.*?)\bendforeach\(\)', text, re.S):
        variable, items, body = loop.groups()
        pattern = r'add_test\(NAME\s+(\w*)\$\{' + re.escape(variable) + r'\}'
        for prefix in re.findall(pattern, body):
            names.update(prefix + item for item in items.split())
    names.update(re.findall(r'add_test\(NAME\s+(\w+)\s', text))
    names.discard(SELF)
    return {name if name.startswith('test_') or name.startswith('addon_radio')
            else 'test_qt_' + name for name in names}


def read_table(path):
    table = {}
    errors = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        where = f'{path.name}:{number}'
        if len(fields) != 3:
            errors.append(f'{where}: expected "name label systems"')
            continue
        name, label, systems = fields
        systems = set(SYSTEMS) if systems == 'all' else set(systems.split(','))
        if label not in LABELS:
            errors.append(f'{where}: unknown label "{label}"')
        if not systems <= set(SYSTEMS):
            errors.append(f'{where}: unknown build system in "{fields[2]}"')
        if name in table:
            errors.append(f'{where}: "{name}" is listed twice')
        table[name] = (label, systems)
    return table, errors


def cmake_name(name):
    return name[len('test_qt_'):] if name.startswith('test_qt_') else name


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--label', choices=LABELS,
                        help='print the tests that carry this label')
    parser.add_argument('--system', choices=SYSTEMS, default='meson',
                        help='build system for --label names (default: meson)')
    args = parser.parse_args()

    table, errors = read_table(root / 'test' / 'addon-tests.txt')
    if args.label:
        for name, (label, systems) in sorted(table.items()):
            if label == args.label and args.system in systems:
                print(cmake_name(name) if args.system == 'cmake' else name)
        return 1 if errors else 0

    qt = root / 'modules' / 'gui' / 'qt'
    automake = automake_tests((qt / 'Makefile.am').read_text()) | \
        automake_tests((root / 'test' / 'Makefile.am').read_text())
    found = {
        'automake': {name for name in automake
                     if name.startswith('test_qt_addon_') or name in table},
        'meson': set().union(*(meson_tests(path.read_text()) for path in (
            qt / 'meson.build', root / 'test' / 'meson.build', root / 'test' / 'src' / 'meson.build',
            root / 'test' / 'modules' / 'meson.build'))),
        'cmake': cmake_tests((qt / 'addons' / 'CMakeLists.txt').read_text()),
    }
    for system in SYSTEMS:
        expected = {name for name, (_, systems) in table.items() if system in systems}
        for name in sorted(found[system] - expected):
            if name in table:
                errors.append(f'{system}: {name} is registered, but its row does not list {system}')
            else:
                errors.append(f'{system}: {name} is registered, but is not in addon-tests.txt')
        for name in sorted(expected - found[system]):
            errors.append(f'{system}: {name} is listed in addon-tests.txt, but is not registered')

    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f'{len(table)} add-on tests agree across Automake, meson and CMake')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
