#!/usr/bin/env python3
"""Contracts for the calibration tools (SDK-free; numpy and scipy, ffmpeg optional).

- Tools/Demos/fit_modal_reference.py: its synthetic self-test, a fit of the
  MusyngKite C4 reference (f0 near 262 Hz, partials near 4.0 and 9.9 x f0) and
  a null comparison of that fit with itself.
- Tools/Demos/build_listening_test.py: BS.1770 loudness calibration, and demo pages
  in both modes: the number of embedded clips, loudness matching, labels only
  in sealed form, ABX truth, seeded determinism and the WAV fallback.

Run: python3 Tests/test_calibration_tools.py   (exits non-zero on failure)
"""
import base64
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DEMOS = ROOT / "Tools" / "Demos"
FIT = DEMOS / "fit_modal_reference.py"
BUILD = DEMOS / "build_listening_test.py"
sys.path.insert(0, str(DEMOS))
import build_listening_test as blt  # noqa: E402

# The demo: trials C4 and C5 with two candidates each, G4 with one.
DEMO_CANDIDATES = (2, 2, 1)


def run(*args, env=None):
    result = subprocess.run([sys.executable, *map(str, args)], capture_output=True, text=True, timeout=180, env=env)
    if result.returncode != 0:
        raise AssertionError(f"{' '.join(map(str, args))} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def page_json(html, element_id):
    match = re.search(rf'<script type="application/json" id="{element_id}">(.*?)</script>', html, re.S)
    return json.loads(match.group(1))


def unseal(text, key):
    """Python mirror of the page's JavaScript: XOR with a xorshift32 keystream."""
    state, out = key, bytearray()
    for byte in base64.b64decode(text):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        out.append(byte ^ (state & 255))
    return json.loads(out.decode())


def wav_samples(uri):
    with wave.open(io.BytesIO(base64.b64decode(uri.split(",", 1)[1]))) as source:
        return np.frombuffer(source.readframes(source.getnframes()), "<i2").astype(np.int32)


class ModalFit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.json_path = Path(cls.tmp.name) / "MusyngKite_C4.json"
        run(FIT, DEMOS / "Reference" / "MusyngKite_C4.wav", "--json", cls.json_path)
        results = json.loads(cls.json_path.read_text())
        assert isinstance(results, list) and len(results) == 1
        cls.c4 = results[0]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_self_test_recovers_known_modes(self):
        output = run(FIT, "--self-test")
        self.assertIn("self-test: passed (0 failures)", output)
        self.assertNotIn("FAIL", output)

    def test_musyngkite_c4_fundamental_and_partials(self):
        r = self.c4
        self.assertEqual((r["note"]["name"], r["note"]["midi"], r["note"]["source"]), ("C4", 60, "file name"))
        self.assertAlmostEqual(r["f0_hz"], 262.0, delta=262.0 * 0.015)
        ratios = [p["ratio"] for p in r["partials"]]
        self.assertTrue(any(abs(x - 4.0) <= 0.08 for x in ratios), f"no partial near 4.0 x f0 in {ratios}")
        self.assertTrue(any(abs(x - 9.9) <= 0.15 for x in ratios), f"no partial near 9.9 x f0 in {ratios}")
        self.assertLessEqual(len(r["partials"]), 12)
        for p in r["partials"]:
            self.assertGreater(p["t60_s"], 0)
            self.assertAlmostEqual(p["eta"] * p["t60_s"] * p["freq_hz"], 2.2, delta=0.01)
        fundamental = next(p for p in r["partials"] if p["ratio"] == 1.0)
        self.assertEqual((fundamental["level_20ms_db"], fundamental["level_100ms_db"]), (0.0, 0.0))
        self.assertTrue(30 <= r["bloom"]["peak_ms"] <= 200, r["bloom"])
        self.assertIsNotNone(r["noise_floor"]["rms_dbfs"])
        self.assertEqual(r["metadata"]["format"], "PCM 16-bit")

    def test_comparison_with_itself_is_null(self):
        diff = json.loads(run(FIT, self.json_path, "--compare-json", self.json_path))["diff"]
        self.assertEqual(diff["f0_cents"], 0.0)
        self.assertEqual(len(diff["partials"]), len(self.c4["partials"]))
        self.assertFalse(diff["unmatched_model_partials"])
        for row in diff["partials"]:
            self.assertEqual((row["ratio_cents"], row["level_20ms_diff_db"], row["level_100ms_diff_db"],
                              row["t60_model_over_ref"]), (0.0, 0.0, 0.0, 1.0))
        table = run(FIT, self.json_path, "--compare-json", self.json_path, "--markdown")
        self.assertIn("| ratio ref | ratio model |", table)


class Loudness(unittest.TestCase):
    def test_k_weighting_is_the_itu_48k_filter(self):
        sos = blt.k_weighting(48000)
        np.testing.assert_allclose(sos[0], [1.53512485958697, -2.69169618940638, 1.19839281085285,
                                            1.0, -1.69065929318241, 0.73248077421585], atol=1e-12)
        np.testing.assert_allclose(sos[1], [1.0, -2.0, 1.0, 1.0, -1.99004745483398, 0.99007225036621], atol=1e-12)

    def test_full_scale_997_hz_sine(self):
        # BS.1770: a 0 dBFS 1 kHz sine in one front channel reads -3.01 LKFS.
        for rate in (44100, 48000, 96000):
            sine = np.sin(2 * np.pi * 997 * np.arange(5 * rate) / rate)
            self.assertAlmostEqual(blt.integrated_loudness(sine, rate), -3.01, delta=0.03)
            self.assertAlmostEqual(blt.integrated_loudness(np.stack([sine, sine], 1), rate), 0.0, delta=0.03)


class ListeningPage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, *args, env=None):
        out = self.dir / f"{name}.html"
        run(BUILD, "--demo", "--out", out, *args, env=env)
        html = out.read_text()
        report = json.loads((self.dir / f"{name}.report.json").read_text())
        return html, report, page_json(html, "test-data"), page_json(html, "test-clips")

    def assert_blind_and_matched(self, html, report, data, clips):
        self.assertEqual(html.count("data:audio/"), len(clips))
        self.assertEqual(report["clips_embedded"], len(clips))
        self.assertEqual(len(set(clips.values())), len(clips), "identical bytes would reveal duplicate slots")
        for trial in report["trials"]:
            finals = [c["final_lufs"] for c in trial["clips"]]
            self.assertLessEqual(max(finals) - min(finals), 0.05, trial)
            self.assertTrue(all(c["peak_dbfs"] <= blt.PEAK_CEILING_DB + 1e-6 for c in trial["clips"]), trial)
            for clip in trial["clips"]:
                self.assertNotIn(clip["label"], html)
                self.assertNotIn(Path(clip["file"]).name, html)
        return unseal(data["sealed"], data["key"])

    def test_demo_mushra_page(self):
        html, report, data, clips = self.build("mushra", "--mode", "mushra")
        expected = sum(3 + n for n in DEMO_CANDIDATES)  # reference, hidden reference, anchor, candidates
        self.assertEqual(len(clips), expected)
        self.assertEqual(len(clips), 14)
        audio = "aac" if shutil.which("ffmpeg") else "wav"
        self.assertEqual(data["build"]["audio"], audio)
        self.assertEqual(html.count("data:audio/mp4" if audio == "aac" else "data:audio/wav"), len(clips))
        sealed = self.assert_blind_and_matched(html, report, data, clips)
        self.assertEqual(len(data["trials"]), 3)
        candidates = {t["id"]: sum(c["role"] == "candidate" for c in t["clips"]) for t in report["trials"]}
        self.assertEqual(sorted(candidates.values()), sorted(DEMO_CANDIDATES))
        for trial in data["trials"]:
            slots = [trial["reference"], *trial["conditions"]]
            self.assertTrue(set(slots) <= set(clips))
            roles = sorted(sealed[trial["id"]]["slots"][s]["role"] for s in slots)
            self.assertEqual(roles, sorted(["reference", "hidden reference", "anchor"]
                                           + ["candidate"] * candidates[trial["id"]]))

    def test_demo_abx_page_truth_determinism_and_wav_fallback(self):
        no_ffmpeg = {"PATH": str(self.dir)}          # an empty PATH: ffmpeg cannot be found
        args = ("--mode", "abx", "--abx-repeats", "2", "--duration", "0.5", "--seed", "5")
        html, report, data, clips = self.build("abx", *args, env=no_ffmpeg)
        self.assertEqual(data["build"]["audio"], "wav")
        self.assertNotIn("data:audio/mp4", html)
        self.assertEqual(len(data["trials"]), sum(DEMO_CANDIDATES) * 2)
        self.assertEqual(len(clips), 3 * sum(DEMO_CANDIDATES) * 2)
        sealed = self.assert_blind_and_matched(html, report, data, clips)
        for trial in data["trials"]:
            truth = sealed[trial["id"]]["x_is"]
            x, same, other = (wav_samples(clips[trial[k]]) for k in ("x", truth.lower(), "ba"[truth == "B"]))
            self.assertLessEqual(np.max(np.abs(x - same)), 2, "X differs from its twin by more than dither")
            self.assertGreater(np.max(np.abs(x - other)), 100, "A and B should be different sounds")
        again = self.build("abx_again", *args, env=no_ffmpeg)[2]
        self.assertEqual(again["trials"], data["trials"])
        other_seed = self.build("abx_seed6", *args[:-1], "6", env=no_ffmpeg)[2]
        self.assertNotEqual(other_seed["trials"], data["trials"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
