"""Prevent combining diagnostic traces with metrics from another framing."""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import compare_runs


class FramingTests(unittest.TestCase):
    def test_same_framing_is_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "metrics.json"
            path.write_text(json.dumps({"blockFrames": 512, "fixtures": []}))
            self.assertEqual(compare_runs.load_metrics(path, 512), [])

    def test_mismatched_or_missing_framing_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "metrics.json"
            for metadata in ({"blockFrames": 128}, {}):
                with self.subTest(metadata=metadata):
                    path.write_text(json.dumps(dict(metadata, fixtures=[])))
                    with self.assertRaises(SystemExit) as error:
                        compare_runs.load_metrics(path, 512)
                    self.assertEqual(error.exception.code, 1)


if __name__ == "__main__":
    unittest.main()
