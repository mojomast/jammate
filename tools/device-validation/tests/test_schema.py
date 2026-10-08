"""Schema documents load and match the records the tools actually write."""
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_helpers as fx  # noqa: E402
import device_lib as dl  # noqa: E402

SCHEMA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          "schema")
FILES = {
    "session.schema.json": dl.SESSION_SCHEMA,
    "latency.schema.json": dl.LATENCY_SCHEMA,
    "play-trial.schema.json": dl.PLAY_TRIAL_SCHEMA,
    "functional.schema.json": dl.FUNCTIONAL_SCHEMA,
    "receipt.schema.json": dl.RECEIPT_SCHEMA,
}


class SchemaTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="devval-schema-")
        self.iface = fx.asio_interface("iface-asio-128", 128)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_schema_ids_match_library(self):
        for name, schema_id in FILES.items():
            doc = dl.load_json_strict(os.path.join(SCHEMA_DIR, name))
            self.assertEqual(doc["$id"], schema_id, name)

    def test_written_records_have_required_top_level_fields(self):
        session = fx.make_session(self.tmp, [self.iface])
        wav = os.path.join(self.tmp, "raw", "clean.wav")
        fx.clean_two_channel(wav)
        latency = fx.latency_record(session, self.tmp, "asio_48k_128",
                                    self.iface, "raw/clean.wav")
        receipt = fx.text_receipt(self.tmp, "log.txt")
        functional = fx.functional_record(session, self.tmp, "silent_input",
                                          self.iface, receipt)
        play = fx.play_record(session, self.tmp, "clean_strumming",
                              self.iface, receipt)
        cases = [("session.schema.json", session),
                 ("latency.schema.json", latency),
                 ("functional.schema.json", functional),
                 ("play-trial.schema.json", play)]
        for name, record in cases:
            doc = dl.load_json_strict(os.path.join(SCHEMA_DIR, name))
            for key in doc["required"]:
                self.assertIn(key, record, "%s missing %s" % (name, key))


if __name__ == "__main__":
    unittest.main()
