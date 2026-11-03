# SPDX-License-Identifier: GPL-2.0-or-later
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_metadata import MARKER, append_metadata, metadata_block, release_metadata
from version import source_generation


class ReleaseMetadataTests(unittest.TestCase):
    def fixture(self, root, generation=0):
        (root / 'meson.build').write_text(f'vlc_update_generation = {generation}\n')
        (root / 'configure.ac').write_text(f'UPDATE_GENERATION={generation}\n')
        (root / 'doc/releases').mkdir(parents=True)

    def test_ordinary_release_needs_no_sidecar(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root)
            result = release_metadata('4.0.0-beta.5', root)
            self.assertEqual(result, dict(schema=1, version='4.0.0-beta.5', generation=0,
                                         product_name='Hakone', package_prefix='hakone', subtitle=''))

    def test_optional_display_fields_and_compiled_generation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root, 1)
            fields = dict(product_name='Sample Player', package_prefix='sample-player',
                          subtitle='日本語のリリース 🎉 <b>shown literally</b> -->')
            (root / 'doc/releases/1.0.0.json').write_text(json.dumps(fields))
            result = release_metadata('1.0.0', root)
            self.assertEqual(result['generation'], 1)
            self.assertEqual(result['version'], '1.0.0')
            for key, value in fields.items():
                self.assertEqual(result[key], value)
            notes = append_metadata('Ordinary notes\n', result)
            self.assertTrue(notes.startswith('Ordinary notes\n\n'))
            self.assertEqual(notes.count('-->'), 1)
            payload = notes.split(MARKER, 1)[1].rsplit('-->', 1)[0]
            self.assertEqual(json.loads(payload), result)

    def test_invalid_or_mismatched_build_generations(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root)
            for value in ('1', '-1', '2147483648', '0; command', 'missing'):
                (root / 'configure.ac').write_text(f'UPDATE_GENERATION={value}\n')
                with self.subTest(value=value), self.assertRaises(ValueError):
                    source_generation(root)

    def test_invalid_sidecars_do_not_silently_publish(self):
        values = [[], None, {'generation': 1}, {'schema': 2}, {'version': '2.0.0'},
                  {'product_name': ''}, {'product_name': 'Name\nInjected'},
                  {'product_name': ' padded '}, {'product_name': 'x' * 129},
                  {'package_prefix': '../player'}, {'package_prefix': 'player/name'},
                  {'package_prefix': 'x' * 65}, {'package_prefix': 1},
                  {'subtitle': 'x' * 513}, {'subtitle': 'line\nline'}, {'subtitle': True}]
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root)
            for value in values:
                (root / 'doc/releases/1.0.0.json').write_text(json.dumps(value))
                with self.subTest(value=value), self.assertRaises(ValueError):
                    release_metadata('1.0.0', root)
            (root / 'doc/releases/1.0.0.json').write_text('{bad')
            with self.assertRaises(ValueError):
                release_metadata('1.0.0', root)

    def test_duplicate_or_oversized_blocks_are_rejected(self):
        with self.assertRaises(ValueError):
            append_metadata(f'Notes\n{MARKER} {{}} -->', {})
        with self.assertRaises(ValueError):
            metadata_block(dict(subtitle='x' * 4096))


if __name__ == '__main__':
    unittest.main()
