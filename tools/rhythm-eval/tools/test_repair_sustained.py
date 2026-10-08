#!/usr/bin/env python3
"""Tests for EVAL-006 `repair_sustained.py` (stdlib unittest, no third party).

They validate the repaired corpus independently of the tool that produced it:

  * identity: the eighteen EVAL-001 fixtures are referenced, not copied, and
    their SHA-256/byte records match the original manifest exactly;
  * preservation: the repaired `sustained_chords` keeps the original event
    times, beat grid, meter and duration;
  * the actual repair: a chord is acoustically present at +0.5 s and +1.5 s
    after every onset, does not clip, and has no edit/splice discontinuity,
    while the ORIGINAL fixture fails the same persistence probes;
  * the independent PCM true-silence derivation: re-derived from the committed
    bytes, the declared spans are quiet, silent, sorted, onset-free, and the
    `silentBeats`/`silenceSpans` missing-onset fields are untouched;
  * reproducibility: `--check` reproduces the committed bytes and manifest, and
    an actual subprocess run produces identical hashes;
  * the tapping audit: onset spacing and post-onset energy are reported, and a
    high silent occupancy is NOT treated as a defect or used to exclude it;
  * meaningful failure detection: truncated duration, fast decay, clipping and
    an injected splice each FAIL the validators that gate the repair.

Run:  python3 tools/rhythm-eval/tools/test_repair_sustained.py
"""

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SCRIPT = os.path.join(REPO, "tools", "rhythm-eval", "tools",
                      "repair_sustained.py")
BASE_DIR = os.path.join(REPO, "testdata", "rhythm")
REPAIRED_DIR = os.path.join(BASE_DIR, "repaired-sustain")


def load_module():
    spec = importlib.util.spec_from_file_location("repair_sustained", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


RS = load_module()


def load_json(path):
    with open(path, "r") as fh:
        return json.load(fh)


def fixture_by_name(manifest, name):
    return next(f for f in manifest["fixtures"] if f["name"] == name)


class Base(unittest.TestCase):
    """Shared, read-only fixtures for the suite."""

    @classmethod
    def setUpClass(cls):
        cls.base = load_json(os.path.join(BASE_DIR, "manifest.json"))
        cls.repaired = load_json(os.path.join(REPAIRED_DIR, "manifest.json"))
        cls.base_sustained = fixture_by_name(cls.base, "sustained_chords")
        cls.repaired_sustained = fixture_by_name(cls.repaired, "sustained_chords")
        cls.repaired_path = os.path.join(REPAIRED_DIR, RS.REPAIRED_WAV_NAME)
        cls.base_sustained_path = os.path.join(BASE_DIR,
                                               cls.base_sustained["file"])
        cls.repaired_samples = RS.read_pcm16(cls.repaired_path)
        cls.base_samples = RS.read_pcm16(cls.base_sustained_path)


# ---------------------------------------------------------------------------
# Corpus identity / preservation
# ---------------------------------------------------------------------------

class CorpusIntegrity(Base):

    def test_new_corpus_id_and_replacement_provenance(self):
        self.assertNotEqual(self.repaired["corpus"]["id"],
                            self.base["corpus"]["id"])
        self.assertEqual(self.repaired["corpus"]["id"], RS.CORPUS_ID)
        self.assertIn("replacement", self.repaired["corpus"])
        repl = self.repaired["corpus"]["replacement"]
        self.assertEqual(repl["fixture"], RS.REPLACED_FIXTURE)
        self.assertEqual(repl["baseWavSha256"], self.base_sustained["sha256"])
        self.assertIn("replacement", self.repaired_sustained)
        self.assertIn("EVAL-006", self.repaired_sustained["provenance"])

    def test_eighteen_originals_referenced_not_copied(self):
        originals = [f for f in self.base["fixtures"]
                     if f["name"] != RS.REPLACED_FIXTURE]
        self.assertEqual(len(originals), 18)
        for base_entry in originals:
            entry = fixture_by_name(self.repaired, base_entry["name"])
            self.assertEqual(entry["file"], "../" + base_entry["file"])
            # Hash record and truth preserved exactly.
            self.assertEqual(entry["sha256"], base_entry["sha256"])
            self.assertEqual(entry["bytes"], base_entry["bytes"])
            self.assertEqual(entry["beats"], base_entry["beats"])
            self.assertEqual(entry["onsets"], base_entry["onsets"])
            self.assertEqual(entry["trueSilenceSpans"],
                             base_entry["trueSilenceSpans"])
            # The referenced file exists where the relative path points.
            target = os.path.normpath(
                os.path.join(REPAIRED_DIR, entry["file"]))
            self.assertTrue(os.path.isfile(target), target)

    def test_only_one_wav_in_repaired_subtree(self):
        wavs = [n for n in os.listdir(REPAIRED_DIR) if n.endswith(".wav")]
        self.assertEqual(wavs, [RS.REPAIRED_WAV_NAME])
        # raw results live in subdirectories, not extra root WAV copies.
        self.assertFalse(os.path.exists(
            os.path.join(REPAIRED_DIR, "sustained_chords.1.wav")))

    def test_repaired_wav_hash_and_size_match_manifest(self):
        self.assertEqual(RS.sha256_of(self.repaired_path),
                         self.repaired_sustained["sha256"])
        self.assertEqual(os.path.getsize(self.repaired_path),
                         self.repaired_sustained["bytes"])

    def test_new_audio_under_size_budget(self):
        self.assertLessEqual(self.repaired_sustained["bytes"], 2 * 1024 * 1024)

    def test_event_times_meter_and_duration_preserved(self):
        for field in ("beats", "onsets", "durationSeconds", "meter",
                      "subdivision", "silentBeats", "silenceSpans", "downbeats",
                      "nominalBpm", "tempoProfile", "scenarioTags"):
            self.assertEqual(self.repaired_sustained[field],
                             self.base_sustained[field], field)

    def test_core_denominator_stays_eleven(self):
        core = self.repaired["tagVocabulary"]["core"]
        self.assertEqual(len(core), 11)
        named = {f["name"] for f in self.repaired["fixtures"]}
        self.assertEqual(set(core) - named, set())
        for name in core:
            self.assertIn("core", fixture_by_name(self.repaired,
                                                  name)["scenarioTags"])

    def test_all_nineteen_original_wav_hashes_are_preserved(self):
        # The repair must never write into `testdata/rhythm/wav`.
        self.assertEqual(
            self.repaired["corpus"]["baseManifestSha256"],
            RS.sha256_of(os.path.join(BASE_DIR, "manifest.json")))
        for base_entry in self.base["fixtures"]:
            path = os.path.join(BASE_DIR, base_entry["file"])
            self.assertEqual(RS.sha256_of(path), base_entry["sha256"],
                             base_entry["name"])
            self.assertEqual(os.path.getsize(path), base_entry["bytes"],
                             base_entry["name"])


# ---------------------------------------------------------------------------
# Acoustic audit of the repair
# ---------------------------------------------------------------------------

class AcousticRepair(Base):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.repaired_levels = RS.frame_levels(cls.repaired_samples, RS.SAMPLE_RATE)
        cls.base_levels = RS.frame_levels(cls.base_samples, RS.SAMPLE_RATE)
        (cls.repaired_threshold, cls.repaired_ref,
         cls.repaired_floor) = RS.pcm_silence_threshold_db(cls.repaired_levels)
        (cls.base_threshold, cls.base_ref,
         cls.base_floor) = RS.pcm_silence_threshold_db(cls.base_levels)

    def test_persistence_at_half_and_one_and_a_half_seconds(self):
        report = RS.validate_persistence(
            self.repaired_samples, RS.SAMPLE_RATE,
            self.repaired_sustained["onsets"], self.repaired_threshold)
        self.assertEqual(report["belowThresholdCount"], 0)
        for row in report["perOnset"]:
            # >0.5 s and >=1.5 s after the attack the chord is still sounding.
            self.assertGreater(row["levelDbfsAtOffset"][0.5],
                               self.repaired_threshold)
            self.assertGreater(row["levelDbfsAtOffset"][1.5],
                               self.repaired_threshold)

    def test_original_fixture_fails_the_same_persistence_probes(self):
        # The defect: by 0.5 s the original chord has already reached the floor.
        self.assertGreater(
            RS.persistence_report(self.base_samples, RS.SAMPLE_RATE,
                                  self.base_sustained["onsets"],
                                  self.base_threshold)["belowThresholdCount"], 0)

    def test_no_clipping(self):
        RS.validate_no_clipping(self.repaired_sustained["signal"])
        self.assertEqual(self.repaired_sustained["signal"][
            "clippedSampleFraction"], 0.0)

    def test_no_onset_discontinuity(self):
        step = RS.validate_continuity(self.repaired_samples)
        self.assertLessEqual(step, RS.MAX_SAMPLE_STEP)
        # A real waveform, not silence padded at the onsets.
        self.assertGreater(step, 0.0)

    def test_repair_changes_only_the_envelope_not_the_events(self):
        # Both files have the same number of declared onsets and the same grid.
        self.assertEqual(len(self.repaired_sustained["onsets"]),
                         len(self.base_sustained["onsets"]))


# ---------------------------------------------------------------------------
# Independent PCM true-silence derivation
# ---------------------------------------------------------------------------

class TrueSilenceCriterion(Base):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.samples = cls.repaired_samples
        cls.spans, cls.audit = RS.compute_pcm_true_silence_spans(
            cls.samples, RS.SAMPLE_RATE, cls.repaired_sustained["durationSeconds"])

    def test_recomputation_matches_the_committed_manifest(self):
        self.assertEqual(self.spans,
                         self.repaired_sustained["trueSilenceSpans"])

    def test_repaired_spans_differ_from_the_defective_original(self):
        self.assertNotEqual(self.repaired_sustained["trueSilenceSpans"],
                            self.base_sustained["trueSilenceSpans"])

    def test_declared_spans_are_actually_quiet(self):
        threshold = self.audit["soundingThresholdDbfs"]
        window = RS.SILENCE_WINDOW_SECONDS
        levels = RS.frame_levels(self.samples, RS.SAMPLE_RATE)
        for a, b in self.spans:
            # Only frames wholly inside the span; a frame straddling the
            # terminating attack is not evidence about the span.
            inside = [level for t, level in levels
                      if a <= t and t + window <= b + 1e-9]
            self.assertTrue(inside, "no frame inside span [%r, %r]" % (a, b))
            self.assertLess(max(inside), threshold,
                            "span [%r, %r] not quiet" % (a, b))

    def test_spans_do_not_contain_any_onset(self):
        RS.validate_true_silence_spans(self.spans,
                                       self.repaired_sustained["onsets"],
                                       self.repaired_sustained["durationSeconds"])

    def test_missing_onset_fields_are_preserved(self):
        # The conceptual "no attack on this beat" spans are a different field
        # and must survive the repair unchanged.
        self.assertEqual(self.repaired_sustained["silentBeats"],
                         self.base_sustained["silentBeats"])
        self.assertEqual(self.repaired_sustained["silenceSpans"],
                         self.base_sustained["silenceSpans"])
        # The distinction the repair exists to make: a missing-onset window is
        # NOT acoustic silence. The chord is still sounding inside it...
        threshold = self.audit["soundingThresholdDbfs"]
        level = RS.window_rms_db(self.samples, RS.SAMPLE_RATE,
                                 self.repaired_sustained["silenceSpans"][0][0],
                                 self.repaired_sustained["silenceSpans"][0][1])
        self.assertGreater(level, threshold)
        # ...and the repaired file's genuine acoustic silence is far smaller
        # than the original's artifact (8.73 s of 11.35 s).
        repaired_silence = sum(b - a for a, b in
                               self.repaired_sustained["trueSilenceSpans"])
        base_silence = sum(b - a for a, b in
                           self.base_sustained["trueSilenceSpans"])
        self.assertGreater(base_silence, 8.0)
        self.assertLess(repaired_silence, 2.0)


# ---------------------------------------------------------------------------
# Meaningful failure detection (the validators can actually fail)
# ---------------------------------------------------------------------------

class ValidatorsCanFail(Base):

    def test_duration_validator_rejects_a_truncated_clip(self):
        with self.assertRaises(AssertionError):
            RS.validate_duration(5.0, 11.35)

    def test_persistence_validator_rejects_fast_decay(self):
        samples = list(self.repaired_samples)
        for i, t in enumerate(self.repaired_sustained["onsets"]):
            start = int((t + 0.4) * RS.SAMPLE_RATE)
            end = int((t + 1.6) * RS.SAMPLE_RATE)
            for j in range(start, min(end, len(samples))):
                samples[j] = 0.0
        (threshold, _ref, _floor) = RS.pcm_silence_threshold_db(
            RS.frame_levels(samples, RS.SAMPLE_RATE))
        with self.assertRaises(AssertionError):
            RS.validate_persistence(samples, RS.SAMPLE_RATE,
                                    self.repaired_sustained["onsets"], threshold)

    def test_continuity_validator_rejects_an_injected_splice(self):
        samples = list(self.repaired_samples)
        i = len(samples) // 2
        samples[i + 1] = samples[i] + 0.95
        with self.assertRaises(AssertionError):
            RS.validate_continuity(samples)

    def test_clipping_validator_rejects_clipped_audio(self):
        with self.assertRaises(AssertionError):
            RS.validate_no_clipping({"clippedSampleFraction": 0.01,
                                     "peakDbfs": 0.0})

    def test_true_silence_validator_rejects_a_span_with_an_onset(self):
        onset = self.repaired_sustained["onsets"][1]
        with self.assertRaises(AssertionError):
            RS.validate_true_silence_spans([[onset - 0.1, onset + 0.1]],
                                           self.repaired_sustained["onsets"],
                                           self.repaired_sustained["durationSeconds"])


# ---------------------------------------------------------------------------
# Tapping audit: high silent occupancy is not a defect
# ---------------------------------------------------------------------------

class TappingAudit(Base):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.tapping = fixture_by_name(cls.base, "tapping_muting_only")
        cls.tapping_path = os.path.join(BASE_DIR, cls.tapping["file"])
        cls.tapping_samples = RS.read_pcm16(cls.tapping_path)
        cls.audit = RS.onset_energy_audit(cls.tapping, cls.tapping_samples,
                                          RS.SAMPLE_RATE)

    def test_tapping_onsets_are_regular_and_energetic(self):
        self.assertEqual(self.audit["onsetCount"], 30)
        self.assertIsNotNone(self.audit["gapStatsSeconds"])
        # Every declared tap produces a large, measurable energy rise.
        self.assertGreater(self.audit["minRiseDb"], 20.0)
        self.assertGreater(self.audit["medianAfterDbfs"], -50.0)

    def test_tapping_is_not_excluded_or_repaired(self):
        # The repair touches only sustained_chords contents.
        repaired_tapping = fixture_by_name(self.repaired, "tapping_muting_only")
        self.assertEqual(repaired_tapping["sha256"], self.tapping["sha256"])
        self.assertIn("../wav/", repaired_tapping["file"])


# ---------------------------------------------------------------------------
# Real CLI evidence: only the sustain fixture moves
# ---------------------------------------------------------------------------

RAW_BASE = os.path.join(REPAIRED_DIR, "raw")
HAVE_RAW = all(os.path.exists(os.path.join(RAW_BASE, corpus, be, "block128",
                                           "results.json"))
               for corpus in ("original", "repaired")
               for be in ("btrack", "aubio"))


@unittest.skipUnless(HAVE_RAW, "committed raw CLI results not present")
class RawComparison(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        def load(corpus, backend):
            with open(os.path.join(RAW_BASE, corpus, backend, "block128",
                                   "results.json")) as fh:
                return json.load(fh)
        cls.results = {(c, b): load(c, b)
                       for c in ("original", "repaired")
                       for b in ("btrack", "aubio")}
        cls.repaired_manifest = load_json(
            os.path.join(REPAIRED_DIR, "manifest.json"))

    def test_core_denominator_unchanged_on_both_backends(self):
        for backend in ("btrack", "aubio"):
            for corpus in ("original", "repaired"):
                agg = self.results[(corpus, backend)]["aggregate"]
                self.assertEqual(agg["fixtures"], 19)
                self.assertEqual(agg["coreFixtures"], 11)

    def test_only_the_sustain_fixture_moves(self):
        # Every other fixture's scored metrics must be byte-identical; only the
        # non-scored wall-clock cpuSeconds may differ between two runs.
        for backend in ("btrack", "aubio"):
            orig = {f["name"]: f for f in
                    self.results[("original", backend)]["fixtures"]}
            rep = {f["name"]: f for f in
                   self.results[("repaired", backend)]["fixtures"]}
            self.assertEqual(set(orig), set(rep))
            for name in orig:
                if name == RS.REPLACED_FIXTURE:
                    continue
                a = {k: v for k, v in orig[name].items() if k != "cpuSeconds"}
                b = {k: v for k, v in rep[name].items() if k != "cpuSeconds"}
                self.assertEqual(a, b, "%s/%s" % (backend, name))

    def test_sustained_fmeasure_improves_for_both_backends(self):
        for backend in ("btrack", "aubio"):
            orig = next(f for f in self.results[("original", backend)]["fixtures"]
                        if f["name"] == RS.REPLACED_FIXTURE)
            rep = next(f for f in self.results[("repaired", backend)]["fixtures"]
                       if f["name"] == RS.REPLACED_FIXTURE)
            self.assertGreater(rep["fMeasure"], orig["fMeasure"],
                               backend)
            self.assertLess(rep["trueSilenceSeconds"],
                            orig["trueSilenceSeconds"])


# ---------------------------------------------------------------------------
# Reproducibility
# ---------------------------------------------------------------------------

class Reproducibility(Base):

    def test_check_reproduces_the_committed_corpus(self):
        self.assertEqual(RS.check(BASE_DIR, REPAIRED_DIR, verbose=False), 0)

    def test_actual_subprocess_run_matches_hashes(self):
        with tempfile.TemporaryDirectory(prefix="eval006-test-") as tmp:
            out = os.path.join(tmp, "out")
            proc = subprocess.run(
                [sys.executable, SCRIPT, "--base", BASE_DIR, "--out", out,
                 "--quiet"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(proc.returncode, 0, proc.stderr.decode())
            produced = load_json(os.path.join(out, "manifest.json"))
            self.assertEqual(
                RS.canonical_json(produced),
                RS.canonical_json(self.repaired))
            self.assertEqual(
                RS.sha256_of(os.path.join(out, RS.REPAIRED_WAV_NAME)),
                RS.sha256_of(self.repaired_path))

    def test_audit_runs_and_reports_both_fixtures(self):
        a = RS.audit(BASE_DIR, REPAIRED_DIR)
        self.assertIn("repaired", a)
        self.assertIn("sustained_chords", a["base"])
        self.assertIn("tapping_muting_only", a["base"])
        self.assertEqual(a["base"]["tapping_muting_only"]["onsetCount"], 30)


if __name__ == "__main__":
    unittest.main(verbosity=2)
