#!/usr/bin/env python3
"""Tests for the EVAL-003 derived-perturbation generator.

These run with the standard library only (`python3 -m unittest`). They are the
generation-side companion to `tests/jam/RhythmDerivedTests.cpp`: that suite
validates what is committed on its own terms, this one re-runs the generator and
checks the transformations against independent acoustic expectations.

Run from the repository root:

    python3 tools/rhythm-eval/tools/test_make_derived.py
    # or
    python3 -m unittest discover -s tools/rhythm-eval/tools -p 'test_*.py'

Nothing here is part of the audio path or the shipped app.
"""

import hashlib
import json
import math
import os
import shutil
import struct
import sys
import tempfile
import unittest
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)

import make_derived as md  # noqa: E402


def repo_path(*parts):
    return os.path.join(REPO_ROOT, *parts)


BASE_DIR = os.environ.get("JAM_RHYTHM_CORPUS", repo_path("testdata/rhythm"))
DERIVED_DIR = os.environ.get("JAM_RHYTHM_DERIVED",
                             repo_path("testdata/rhythm/derived"))


def read_wav_pcm(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        return list(struct.unpack("<%dh" % w.getnframes(),
                                  w.readframes(w.getnframes())))


def rms(values):
    if not values:
        return 0.0
    return math.sqrt(sum(v * v for v in values) / len(values))


class DerivedManifestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(DERIVED_DIR, "manifest.json")) as fh:
            cls.manifest = json.load(fh)
        cls.by_name = {f["name"]: f for f in cls.manifest["fixtures"]}
        with open(os.path.join(BASE_DIR, "manifest.json")) as fh:
            base = json.load(fh)
        cls.base = {f["name"]: f for f in base["fixtures"]}

    def file_path(self, fixture):
        return os.path.join(DERIVED_DIR, fixture["file"])

    def baseline_for(self, fixture):
        return self.by_name[fixture["pairedBaseline"]]

    # -- integrity ---------------------------------------------------------

    def test_committed_hashes_match_bytes(self):
        for f in self.manifest["fixtures"]:
            path = self.file_path(f)
            self.assertTrue(os.path.exists(path), path)
            with open(path, "rb") as fh:
                digest = hashlib.sha256(fh.read()).hexdigest()
            self.assertEqual(digest, f["sha256"], f["name"])
            self.assertEqual(os.path.getsize(path), f["bytes"], f["name"])

    def test_parent_hashes_inherited(self):
        for f in self.manifest["fixtures"]:
            self.assertEqual(f["parentSha256"],
                             self.base[f["parentFixture"]]["sha256"], f["name"])

    def test_tags_closed(self):
        vocab = set(self.manifest["tagVocabulary"]["perturbation"])
        quals = set(self.manifest["tagVocabulary"]["qualifier"])
        for f in self.manifest["fixtures"]:
            self.assertIn(f["scenarioTags"][0], vocab, f["name"])
            for tag in f["scenarioTags"][1:]:
                self.assertIn(tag, quals, f["name"])

    def test_disk_budget(self):
        total = sum(f["bytes"] for f in self.manifest["fixtures"])
        self.assertLessEqual(total, 15 * 1024 * 1024,
                             "derived audio exceeds the 15 MB budget")

    # -- acoustics ---------------------------------------------------------

    def test_baseline_is_an_exact_parent_slice(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] != "baseline":
                continue
            parent = self.base[f["parentFixture"]]
            parent_pcm = read_wav_pcm(os.path.join(BASE_DIR, parent["file"]))
            a = f["truncation"]["sourceStartFrame"]
            b = a + f["truncation"]["frames"]
            self.assertEqual(read_wav_pcm(self.file_path(f)),
                             parent_pcm[a:b], f["name"])

    def test_noise_snr_matches_declaration(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] != "noise":
                continue
            base_pcm = read_wav_pcm(self.file_path(self.baseline_for(f)))
            pcm = read_wav_pcm(self.file_path(f))
            noise = [a - b for a, b in zip(pcm, base_pcm)]
            snr = 20.0 * math.log10(rms(base_pcm) / rms(noise))
            self.assertAlmostEqual(snr, f["declaredSnrDb"], delta=0.15, msg=f["name"])
            self.assertAlmostEqual(f["signal"]["measuredSnrDb"],
                                   f["declaredSnrDb"], delta=0.15, msg=f["name"])

    def test_level_scaling_is_exact(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] != "level":
                continue
            base_pcm = read_wav_pcm(self.file_path(self.baseline_for(f)))
            pcm = read_wav_pcm(self.file_path(f))
            gain = 10.0 ** (f["declaredGainDb"] / 20.0)
            mismatches = 0
            for a, b in zip(pcm, base_pcm):
                want = int(math.floor(max(-1.0, min(1.0, b / 32767.0 * gain))
                                      * 32767.0 + 0.5))
                if abs(a - want) > 1:
                    mismatches += 1
            self.assertEqual(mismatches, 0, f["name"])

    def test_clipping_fraction_matches_and_is_monotone(self):
        by_parent = {}
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] != "clipping":
                continue
            pcm = read_wav_pcm(self.file_path(f))
            threshold = f["transformation"]["thresholdFraction"]
            ceiling = int(math.floor(threshold * 32767.0 + 0.5))
            hits = sum(1 for v in pcm if abs(abs(v) - ceiling) <= 1)
            frac = hits / len(pcm)
            self.assertAlmostEqual(frac, f["signal"]["clippedSampleFraction"],
                                   delta=1e-6, msg=f["name"])
            by_parent.setdefault(f["parentFixture"], []).append((threshold, frac))
        for parent, points in by_parent.items():
            points.sort(reverse=True)  # highest threshold first
            fracs = [p[1] for p in points]
            self.assertEqual(fracs, sorted(fracs),
                             "clipped fraction must rise as the ceiling falls: %s"
                             % parent)
            self.assertGreater(fracs[-1], 0.0, parent)

    def test_onset_offset_is_an_exact_sample_delay(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] != "onset_offset":
                continue
            n = int(round(f["transformation"]["offsetSeconds"] * 48000))
            self.assertGreater(n, 0)
            base_pcm = read_wav_pcm(self.file_path(self.baseline_for(f)))
            pcm = read_wav_pcm(self.file_path(f))
            self.assertEqual(len(pcm), len(base_pcm), f["name"])
            self.assertEqual(pcm[:n], [0] * n, f["name"])
            self.assertEqual(pcm[n:], base_pcm[:len(base_pcm) - n], f["name"])

    def test_drop_and_burst_edit_audio_only_at_the_edit(self):
        for f in self.manifest["fixtures"]:
            kind = f["scenarioTags"][0]
            if kind not in ("drop_onset", "syncopation_burst"):
                continue
            base_pcm = read_wav_pcm(self.file_path(self.baseline_for(f)))
            pcm = read_wav_pcm(self.file_path(f))
            self.assertEqual(len(pcm), len(base_pcm), f["name"])
            if kind == "drop_onset":
                for t in f["transformation"]["droppedOnsets"]:
                    a = max(0, int(t * 48000) - 240)
                    b = int(t * 48000) + 2880
                    self.assertLess(rms(pcm[a:b]), rms(base_pcm[a:b]), f["name"])
            else:
                for t in f["transformation"]["injectedOnsets"]:
                    a = int(t * 48000)
                    b = a + 2880
                    self.assertGreater(rms(pcm[a:b]), rms(base_pcm[a:b]), f["name"])

    # -- truth semantics ---------------------------------------------------

    def test_amplitude_perturbations_inherit_truth_exactly(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] not in ("noise", "level", "clipping",
                                            "baseline"):
                continue
            base = self.baseline_for(f)
            self.assertEqual(f["beats"], base["beats"], f["name"])
            self.assertEqual(f["onsets"], base["onsets"], f["name"])
            self.assertEqual(f["groundTruth"]["policy"], "inherit", f["name"])
            self.assertFalse(f["groundTruth"]["timingAffectsTruth"], f["name"])

    def test_shift_perturbations_move_truth_by_the_shift(self):
        for f in self.manifest["fixtures"]:
            kind = f["scenarioTags"][0]
            if kind not in ("onset_offset", "leading_silence"):
                continue
            shift = (f["transformation"]["offsetSeconds"] if kind == "onset_offset"
                     else f["transformation"]["silenceSeconds"])
            base = self.baseline_for(f)
            for got, want in zip(f["beats"], [b + shift for b in base["beats"]]):
                self.assertAlmostEqual(got, want, places=9, msg=f["name"])
            for got, want in zip(f["onsets"], [o + shift for o in base["onsets"]]):
                self.assertAlmostEqual(got, want, places=9, msg=f["name"])

    def test_tempo_step_warp_is_coherent(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] != "tempo_step":
                continue
            ratio = f["transformation"]["ratio"]
            Ts = f["transformation"]["stepTimeSeconds"]
            base = self.baseline_for(f)
            self.assertEqual(len(f["beats"]), len(base["beats"]), f["name"])
            for got, t in zip(f["beats"], base["beats"]):
                want = t if t <= Ts else Ts + (t - Ts) / ratio
                self.assertAlmostEqual(got, want, places=6, msg=f["name"])
            # Spacing after the anchor equals spacing before divided by the ratio.
            for i in range(1, len(base["beats"]) - 1):
                if base["beats"][i] > Ts + 1e-9:
                    before = base["beats"][i] - base["beats"][i - 1]
                    after = f["beats"][i + 1] - f["beats"][i]
                    self.assertAlmostEqual(after, before / ratio, places=6,
                                           msg=f["name"])

    def test_evidence_edits_keep_the_grid(self):
        for f in self.manifest["fixtures"]:
            if f["scenarioTags"][0] not in ("drop_onset", "syncopation_burst",
                                            "silence_gap"):
                continue
            base = self.baseline_for(f)
            self.assertEqual(f["beats"], base["beats"], f["name"])


class GenerationDeterminismTest(unittest.TestCase):
    def test_two_generations_are_byte_identical(self):
        with tempfile.TemporaryDirectory(prefix="ev003-det-") as a, \
                tempfile.TemporaryDirectory(prefix="ev003-det-") as b:
            md.build(BASE_DIR, a, verbose=False)
            md.build(BASE_DIR, b, verbose=False)
            for name in sorted(os.listdir(a)):
                with open(os.path.join(a, name), "rb") as fa, \
                        open(os.path.join(b, name), "rb") as fb:
                    self.assertEqual(fa.read(), fb.read(), name)

    def test_check_mode_accepts_committed_set(self):
        self.assertEqual(md.check(BASE_DIR, DERIVED_DIR, verbose=False), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
