"""Regression tests for the warning catalog and frozen-build resource lookup."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
import warnings
from pathlib import Path
from unittest.mock import patch

from lynxer import lynxer as runtime


class WarningMessagesTests(unittest.TestCase):
    def test_catalog_contains_every_warning_key(self):
        expected = {
            "legacy_is",
            "legacy_not_is",
            "legacy_tuple",
            "legacy_vargroup",
            "forever_no_break",
        }
        self.assertEqual(set(runtime._WARNING_MESSAGES), expected)
        for key in expected:
            self.assertTrue(runtime.warning_message(key))

    def test_legacy_syntax_warnings_use_catalog_messages(self):
        source = """
global setup(){}
global main(){
    assert(1 is 1);
    assert(1 not is 2);
    tuple legacyTuple = [int 1, int 2];
    vargroup legacyGroup = [str name = "legacy"];
}
"""
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            _, error = runtime.run("warning-test.lynx", source)

        self.assertIsNone(error)
        messages = [str(item.message) for item in caught]
        self.assertIn(runtime.warning_message("legacy_is"), messages)
        self.assertIn(runtime.warning_message("legacy_not_is"), messages)
        self.assertIn(runtime.warning_message("legacy_tuple"), messages)
        self.assertIn(runtime.warning_message("legacy_vargroup"), messages)

    def test_frozen_bundle_catalog_fallback(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            bundled_package = Path(temp_dir) / "lynxer"
            bundled_package.mkdir()
            bundled_catalog = bundled_package / "warnings.txt"
            bundled_catalog.write_text(
                "frozen_test\tLoaded from the frozen bundle.\n",
                encoding="utf-8",
            )

            missing_source_catalog = os.path.join(temp_dir, "missing", "warnings.txt")
            with patch.object(runtime, "_WARNING_MESSAGES_PATH", missing_source_catalog):
                with patch.object(sys, "_MEIPASS", temp_dir, create=True):
                    messages = runtime._load_warning_messages()

        self.assertEqual(messages, {"frozen_test": "Loaded from the frozen bundle."})

    def test_both_build_targets_bundle_warning_catalog(self):
        makefile = Path(__file__).parents[1] / "Makefile"
        text = makefile.read_text(encoding="utf-8")

        normal_target = text.split("build:", 1)[1].split("buildLite:", 1)[0]
        lite_target = text.split("buildLite:", 1)[1].split("clean:", 1)[0]
        self.assertIn("$(WARNING_DATA)", normal_target)
        self.assertIn("$(WARNING_DATA)", lite_target)
        self.assertIn("--add-data \"$(WARNING_FILE):lynxer\"", text)


if __name__ == "__main__":
    unittest.main()