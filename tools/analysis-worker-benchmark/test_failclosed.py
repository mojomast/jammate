"""Exercise invalid input, failed backends and incomplete benchmark output."""
import argparse
import csv
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

OPTIONS = None
HERE = pathlib.Path(__file__).resolve().parent


class BenchmarkChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="analysis-benchmark-checks-")
        cls.root = pathlib.Path(cls.directory.name)
        source = cls.root / "backend.cpp"
        source.write_text(r'''
#include "jam/IRhythmTracker.h"
#include <stdexcept>
class Tracker : public jam::IRhythmTracker {
public:
    void reset(double) override {
#ifdef RESET_FAILURE
        throw std::runtime_error("reset failure");
#endif
    }
    jam::RhythmObservation process(const jam::AnalysisFrame& frame) override {
#ifdef PROCESS_FAILURE
        throw std::runtime_error("process failure");
#endif
        jam::RhythmObservation result {};
        result.inputSampleTime = frame.sampleTime;
        result.sourceSampleRate = frame.sourceSampleRate;
        result.beatEvent = true;
        return result;
    }
    const char* id() const noexcept override { return "scripted"; }
};
extern "C" jam::IRhythmTracker* jam_rhythm_create() {
#ifdef NULL_FACTORY
    return nullptr;
#else
    return new Tracker;
#endif
}
extern "C" void jam_rhythm_destroy(jam::IRhythmTracker* tracker) { delete tracker; }
''')
        cls.plugins = {}
        for mode in ("normal", "RESET_FAILURE", "PROCESS_FAILURE", "NULL_FACTORY"):
            plugin = cls.root / (mode + ".so")
            command = [OPTIONS.cxx, "-std=c++17", "-shared", "-fPIC", "-I" + str(HERE.parents[1] / "src")]
            if mode != "normal":
                command.append("-D" + mode)
            subprocess.run(command + [str(source), "-o", str(plugin)], check=True,
                           capture_output=True, text=True)
            cls.plugins[mode] = plugin

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_benchmark(self, extra=(), plugin="normal", output=None):
        output = output or self.root / self.id().split(".")[-1]
        output.mkdir(exist_ok=True)
        command = [OPTIONS.binary, "--plugin=scripted=" + str(self.plugins[plugin]),
                   "--rate=48000", "--seconds=0.1", "--block-frames=128",
                   "--ring-capacity=64", "--burst=32", "--mode=both", "--out=" + str(output)]
        result = subprocess.run(command + list(extra), capture_output=True, text=True, timeout=10)
        return result, output

    def test_invalid_arguments_reject_before_backend_load(self):
        cases = ["--block-frames=0", "--block-frames=2049", "--block-frames=128x",
                 "--block-frames=-1", "--burst=0", "--burst=65", "--burst=33",
                 "--ring-capacity=0", "--ring-capacity=999999999999999999999999",
                 "--mode=invalid", "--rate=nan", "--rate=inf", "--rate=-1",
                 "--rate=48000garbage", "--rate=1000000", "--seconds=0",
                 "--seconds=999", "--seconds=0.000001", "--bpm=nan", "--bpm=0"]
        for argument in cases:
            with self.subTest(argument=argument):
                result, _ = self.run_benchmark([argument])
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_failed_backends_do_not_emit_successful_measurements(self):
        for mode in ("RESET_FAILURE", "PROCESS_FAILURE", "NULL_FACTORY"):
            with self.subTest(mode=mode):
                result, output = self.run_benchmark(plugin=mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse((output / "benchmark.json").exists())

    def test_missing_plugin_is_failure(self):
        result, _ = self.run_benchmark(["--plugin=missing=/missing/plugin.so"])
        self.assertEqual(result.returncode, 1, result.stderr)

    def test_pressure_retains_first_capacity_and_counts_losses(self):
        result, output = self.run_benchmark()
        self.assertEqual(result.returncode, 0, result.stderr)
        records = json.loads((output / "benchmark.json").read_text())["runs"]
        self.assertEqual(len(records), 2)
        for record in records:
            self.assertTrue(record["ready"])
            self.assertEqual(record["observations"] + record["droppedObservations"], record["blocks"])
            self.assertEqual(record["beats"], record["observations"])
            if record["mode"] == "pressure":
                self.assertEqual(record["observations"], 32)
                self.assertEqual(record["droppedObservations"], 5)
            else:
                self.assertEqual(record["droppedObservations"], 0)
        with (output / "throughput.csv").open(newline="") as file:
            self.assertEqual(len(list(csv.DictReader(file))), 2)

    @unittest.skipUnless(os.path.exists("/dev/full"), "Linux output-failure control")
    def test_output_flush_failure_is_not_success(self):
        output = self.root / "unwriteable"
        output.mkdir()
        (output / "throughput.csv").symlink_to("/dev/full")
        result, _ = self.run_benchmark(output=output)
        self.assertEqual(result.returncode, 3, result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__] + remaining)
