# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[4] / "modules/gui/qt/scripts/subtitle-consumer-key.py"


class ConsumerKeyTests(unittest.TestCase):
    def generate(self, directory, *, filename=None, contents="", environment_key=None):
        directory = Path(directory)
        env = dict(os.environ)
        env.pop("OPENSUBTITLES_API_KEY", None)
        env.pop("OPENSUBTITLES_API_KEY_FILE", None)
        if environment_key is not None:
            env["OPENSUBTITLES_API_KEY"] = environment_key
        if filename:
            source = directory / filename
            source.write_text(contents)
            env["OPENSUBTITLES_API_KEY_FILE"] = str(source)
        output = directory / "consumer.hpp"
        done = subprocess.run([sys.executable, str(SCRIPT), "--output", str(output)],
                              env=env, text=True, capture_output=True, timeout=5)
        return done, output

    def test_unrelated_named_secret_is_never_treated_as_an_opaque_consumer_key(self):
        for filename in (".env", ".env.local", "settings.env", "opaque.key"):
            for contents in ("OTHER_USER_KEY=synthetic-private-test-value",
                             "export OTHER_USER_KEY=synthetic-private-test-value",
                             "OTHER_USER_KEY = synthetic-private-test-value"):
                with self.subTest(filename=filename, contents=contents), tempfile.TemporaryDirectory() as temp:
                    done, output = self.generate(temp, filename=filename, contents=contents,
                                                 environment_key="explicit-global-test-value")
                    self.assertNotEqual(done.returncode, 0)
                    self.assertFalse(output.exists())
                    self.assertIn("no OPENSUBTITLES_API_KEY entry", done.stderr)
                    self.assertNotIn("synthetic-private-test-value", done.stdout + done.stderr)
                    self.assertNotIn("OTHER_USER_KEY", done.stdout + done.stderr)

    def test_dotenv_filename_requires_exact_named_entry_even_for_one_line(self):
        with tempfile.TemporaryDirectory() as temp:
            done, output = self.generate(temp, filename=".env", contents="synthetic-opaque-value")
            self.assertNotEqual(done.returncode, 0)
            self.assertFalse(output.exists())
            self.assertNotIn("synthetic-opaque-value", done.stdout + done.stderr)

    def test_explicit_consumer_entry_and_unambiguous_raw_file_remain_supported(self):
        for filename, contents in (
            ("consumer.key", "synthetic-consumer-value"),
            (".env", 'OPENSUBTITLES_API_KEY="synthetic-consumer-value"'),
            (".env", "# build settings\nOTHER_USER_KEY=synthetic-private-value\n"
                     "export OPENSUBTITLES_API_KEY='synthetic-consumer-value'\n")
        ):
            with self.subTest(filename=filename, contents=contents), tempfile.TemporaryDirectory() as temp:
                done, output = self.generate(temp, filename=filename, contents=contents)
                self.assertEqual(done.returncode, 0, done.stderr)
                self.assertIn('"synthetic-consumer-value"', output.read_text())
                self.assertNotIn("synthetic-private-value", output.read_text())
                self.assertEqual(done.stdout, "")

    def test_no_key_still_generates_an_empty_optional_consumer_key(self):
        with tempfile.TemporaryDirectory() as temp:
            done, output = self.generate(temp)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertIn('subtitleConsumerKey[] = "";', output.read_text())
            self.assertEqual(done.stdout, "")


if __name__ == "__main__":
    unittest.main()
