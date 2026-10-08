"""Tests for style_catalog_provenance.py (STYLE-DIRECTOR-002).

Run:  python3 -m unittest discover tools/style-catalog/tests
"""

import importlib.util
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.abspath(os.path.join(HERE, "..", "style_catalog_provenance.py"))

spec = importlib.util.spec_from_file_location("style_catalog_provenance", TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class FnvTests(unittest.TestCase):
    def test_known_vectors(self):
        self.assertEqual(tool.fnv1a32(""), 0x811C9DC5)
        self.assertEqual(tool.fnv1a32("abc"), 0x1A47E90B)


class LibraryParseTests(unittest.TestCase):
    def setUp(self):
        self.src, self.library = tool.parse_library()

    def test_parses_whole_library(self):
        self.assertEqual(len(self.library), 570)

    def test_duplicate_name_is_kind_split(self):
        # ("FUNK", "Linear funk") is a groove at 63 and a fill at 187.
        self.assertEqual(self.library[63]["genre"], "FUNK")
        self.assertEqual(self.library[63]["name"], "Linear funk")
        self.assertFalse(self.library[63]["fill"])
        self.assertEqual(self.library[187]["name"], "Linear funk")
        self.assertTrue(self.library[187]["fill"])

    def test_shuffle_rock_duplicate_has_distinct_specs(self):
        # ROCK / Shuffle rock at 9 and 535 differ.
        self.assertNotEqual(self.library[9]["spec"], self.library[535]["spec"])


class CatalogCheckTests(unittest.TestCase):
    def setUp(self):
        self.src, self.library = tool.parse_library()
        _, self.refs = tool.parse_catalog_refs()

    def test_shipped_catalog_matches_library(self):
        ok, problems = tool.check_catalog(self.library, self.refs)
        self.assertTrue(ok, problems)

    def test_mismatch_is_detected(self):
        mutated = [dict(ref) for ref in self.refs]
        mutated[0]["name"] = "Not the real name"
        ok, problems = tool.check_catalog(self.library, mutated)
        self.assertFalse(ok)
        self.assertTrue(problems)

    def test_fingerprint_matches_source(self):
        declared = tool.declared_fingerprint()
        import hashlib
        self.assertEqual(declared, hashlib.sha256(self.src.encode("utf-8")).hexdigest())

    def test_check_mode_passes(self):
        self.assertEqual(tool.run(emit=False), 0)


if __name__ == "__main__":
    sys.exit(unittest.main())
