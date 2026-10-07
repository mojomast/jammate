"""Contract tests: the scorer must reject anything that is not a declared real run."""

import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "beatnet_eval"))
import io_contract  # noqa: E402

H = "a" * 64  # placeholder 64-hex


def valid_doc():
    return {
        "schema": io_contract.SCHEMA,
        "provenance": {
            "source": "BeatNet",
            "repo_commit": "81cedd4beeb7235262db80969a0c9ce9a48a0ed4",
            "mode": "realtime",
            "inference_model": "PF",
            "device": "cpu",
            "raw_output_sha256": H,
            "driver_sha256": H,
        },
        "availability_semantics": "per_chunk",
        "fixtures": [{
            "name": "clean_eighths",
            "input_wav_sha256": H,
            "beats": [{"time": 0.35, "available": 0.44}],
        }],
    }


class ContractTest(unittest.TestCase):
    def test_valid_passes(self):
        io_contract.validate_observations(valid_doc())

    def test_missing_provenance_rejected(self):
        doc = valid_doc()
        del doc["provenance"]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_wrong_source_rejected(self):
        doc = valid_doc()
        doc["provenance"]["source"] = "synthetic"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_bad_commit_rejected(self):
        doc = valid_doc()
        doc["provenance"]["repo_commit"] = "not-a-sha"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_missing_raw_hash_rejected(self):
        doc = valid_doc()
        del doc["provenance"]["raw_output_sha256"]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_missing_driver_hash_rejected(self):
        doc = valid_doc()
        del doc["provenance"]["driver_sha256"]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_online_batch_must_not_carry_availability(self):
        doc = valid_doc()
        doc["provenance"]["mode"] = "online"
        doc["availability_semantics"] = "online_batch"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_per_chunk_requires_some_availability(self):
        doc = valid_doc()
        doc["fixtures"][0]["beats"] = [{"time": 0.35, "available": None}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_bad_wav_hash_rejected(self):
        doc = valid_doc()
        doc["fixtures"][0]["input_wav_sha256"] = "short"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_no_bypass_exists(self):
        self.assertFalse(hasattr(io_contract, "allow_unverified"))


if __name__ == "__main__":
    unittest.main()
