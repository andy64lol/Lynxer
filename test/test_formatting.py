"""Regression tests for Lynxer formatting and lint CLI behavior."""

from __future__ import annotations

import unittest

from lynxer.formatting import format_source, lint_source


SOURCE = """global setup(){}
global main(){int x=1+2;if(x>0){println("ok");}// keep this comment
}
"""


class FormattingTests(unittest.TestCase):
    def test_pretty_format_is_valid_and_idempotent(self):
        formatted = format_source("format-test.lynx", SOURCE)
        self.assertIn("// keep this comment", formatted)
        self.assertGreater(formatted.count("\n"), 3)
        self.assertIsNone(lint_source("format-test.lynx", formatted))
        self.assertEqual(format_source("format-test.lynx", formatted), formatted)

    def test_oneline_format_has_no_source_newlines(self):
        formatted = format_source("format-test.lynx", SOURCE, oneline=True)
        self.assertNotIn("\n", formatted)
        self.assertNotIn("// keep this comment", formatted)
        self.assertIsNone(lint_source("format-test.lynx", formatted))

    def test_lint_reports_invalid_syntax(self):
        error = lint_source("broken.lynx", "global main( {")
        self.assertIsNotNone(error)
        self.assertIn("Expected", error.details)


if __name__ == "__main__":
    unittest.main()