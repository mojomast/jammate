"""Contract tests.

These check the metadata/format gate and the mode semantics. Passing this gate
does NOT prove a run is authentic -- it only means the declared metadata is
well-formed and internally consistent. The synthetic documents below are unit
fixtures, not BeatNet observations.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "beatnet_eval"))
import io_contract  # noqa: E402

H = "a" * 64  # placeholder 64-hex


def valid_per_chunk():
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


def valid_online():
    return {
        "schema": io_contract.SCHEMA,
        "provenance": {
            "source": "BeatNet",
            "repo_commit": "81cedd4beeb7235262db80969a0c9ce9a48a0ed4",
            "mode": "online",
            "inference_model": "PF",
            "device": "cpu",
            "raw_output_sha256": H,
            "driver_sha256": H,
        },
        "availability_semantics": "online_batch",
        "fixtures": [{
            "name": "clean_eighths",
            "input_wav_sha256": H,
            "beats": [{"time": 0.35, "available": None}],
        }],
    }


class ContractTest(unittest.TestCase):
    def test_valid_per_chunk_passes(self):
        io_contract.validate_observations(valid_per_chunk())

    def test_valid_online_batch_passes(self):
        io_contract.validate_observations(valid_online())

    def test_zero_beat_run_is_allowed(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = []
        io_contract.validate_observations(doc)

    def test_missing_provenance_rejected(self):
        doc = valid_per_chunk()
        del doc["provenance"]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_non_beatnet_source_rejected(self):
        doc = valid_per_chunk()
        doc["provenance"]["source"] = "synthetic"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_bad_commit_rejected(self):
        doc = valid_per_chunk()
        doc["provenance"]["repo_commit"] = "not-a-sha"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_missing_raw_hash_rejected(self):
        doc = valid_per_chunk()
        del doc["provenance"]["raw_output_sha256"]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_missing_driver_hash_rejected(self):
        doc = valid_per_chunk()
        del doc["provenance"]["driver_sha256"]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    # --- mode semantics -----------------------------------------------------

    def test_per_chunk_rejects_online(self):
        doc = valid_per_chunk()
        doc["provenance"]["mode"] = "online"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_per_chunk_rejects_offline(self):
        doc = valid_per_chunk()
        doc["provenance"]["mode"] = "offline"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_per_chunk_rejects_dbn(self):
        doc = valid_per_chunk()
        doc["provenance"]["inference_model"] = "DBN"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_offline_mode_rejected(self):
        doc = valid_per_chunk()
        doc["provenance"]["mode"] = "offline"
        doc["provenance"]["inference_model"] = "DBN"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_online_batch_rejects_realtime_mode(self):
        doc = valid_online()
        doc["provenance"]["mode"] = "realtime"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_online_batch_rejects_non_null_availability(self):
        doc = valid_online()
        doc["fixtures"][0]["beats"] = [{"time": 0.35, "available": 0.40}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    # --- observation value rules -------------------------------------------

    def test_per_chunk_requires_non_null_availability(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = [{"time": 0.35, "available": None}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_available_before_time_rejected(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = [{"time": 0.50, "available": 0.40}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_available_equal_time_allowed(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = [{"time": 0.40, "available": 0.40}]
        io_contract.validate_observations(doc)

    def test_nan_time_rejected(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = [{"time": float("nan"), "available": 0.44}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_inf_time_rejected(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = [{"time": float("inf"), "available": float("inf")}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_bool_time_rejected(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["beats"] = [{"time": True, "available": 0.44}]
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_bad_wav_hash_rejected(self):
        doc = valid_per_chunk()
        doc["fixtures"][0]["input_wav_sha256"] = "short"
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_duplicate_fixture_names_rejected(self):
        doc = valid_per_chunk()
        doc["fixtures"].append(dict(doc["fixtures"][0]))
        with self.assertRaises(io_contract.InputContractError):
            io_contract.validate_observations(doc)

    def test_no_bypass_exists(self):
        self.assertFalse(hasattr(io_contract, "allow_unverified"))


if __name__ == "__main__":
    unittest.main()
