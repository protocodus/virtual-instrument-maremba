#!/usr/bin/env python3
"""SDK-free regression contracts for demo parsing, timing and output validation."""
import hashlib
import inspect
import json
import struct
import subprocess
import tempfile
import unittest
import sys
import wave
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools" / "Demos"))
import run_benchmark as demos  # noqa: E402


class MidiContracts(unittest.TestCase):
    @staticmethod
    def smf(track, division=480):
        return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, division) +
                b"MTrk" + struct.pack(">I", len(track)) + track)

    def test_tempo_running_status_and_zero_velocity(self):
        track = (b"\x00\xff\x51\x03\x07\xa1\x20"  # 500000 us/quarter
                 b"\x00\x90\x3c\x64"                 # on C4 at 0
                 b"\x81\x70\x40\x50"                 # running-status E4 at 240 ticks
                 b"\x81\x70\x3c\x00"                 # velocity zero = off at 480
                 b"\x00\xff\x2f\x00")
        self.assertEqual(demos.parse_smf(self.smf(track)),
                         [(0.0, 1, 60, 100), (0.25, 1, 64, 80), (0.5, 0, 60, 0)])

    def test_sysex_uses_length_including_embedded_f7(self):
        track = b"\x00\xf0\x03\x01\xf7\x02\x00\x90\x3c\x64\x00\xff\x2f\x00"
        self.assertEqual(demos.parse_smf(self.smf(track)), [(0.0, 1, 60, 100)])

    def test_invalid_input_fails(self):
        bad = [b"", b"MThd", self.smf(b"\x00\x3c\x64"), self.smf(b"\x00\x90\x3c"),
               self.smf(b"\x00\xff\x51\x03\x00\x00\x00"),
               self.smf(b"\x81\x81\x81\x81\x00\x90\x3c\x64"),
               self.smf(b"\x00\x90\x3c\x64", 0x8001)]
        for source in bad:
            with self.subTest(source=source), self.assertRaises(ValueError):
                demos.parse_smf(source)

    def test_all_cached_sequences_parse_and_match_provenance(self):
        demos.ensure_midi_files()
        for song in demos.SONGS.values():
            with self.subTest(song=song["filename"]):
                events = demos.parse_smf((demos.MIDI_DIR / song["filename"]).read_bytes())
                self.assertGreater(len(events), 100)
                self.assertEqual(events, sorted(events, key=lambda row: row[0]))


class DemoPlanContracts(unittest.TestCase):
    @staticmethod
    def jobs():
        signature = inspect.signature(demos.render_one)
        return [signature.bind(*job).arguments for job in demos.render_jobs(45, False, 0)]

    def test_existing_production_tracks_remain_and_repeated_models_are_added(self):
        jobs = self.jobs()
        self.assertEqual(len(jobs), 62)
        filenames = {f"{j['key']}_{j['model']['code']}{j['tag']}.wav" for j in jobs}
        self.assertEqual(len(filenames), 62)
        manifest = json.loads(demos.MANIFEST.read_text())
        existing = {r["wav"] for r in manifest["renders"]
                    if r["song_key"] != demos.REPEATED_STUDY_KEY and r["category"] != demos.IDIOM_CATEGORY}
        self.assertEqual(len(existing), 52)
        self.assertTrue(existing <= filenames)
        repeated = [j for j in jobs if j["key"] == demos.REPEATED_STUDY_KEY]
        self.assertEqual({j["model"]["id"] for j in repeated}, {0, 1, 2, 3})
        for job in repeated:
            self.assertEqual(job["category"], "Touch")
            self.assertEqual(job["position"], 0.5)  # Enables --controlled-strike.
            self.assertEqual(job["duration"], 9)
            self.assertEqual(job["mallet"], job["model"]["default_mallet"])

    def test_idiom_studies_are_original_and_fit_their_render_window(self):
        jobs = [j for j in self.jobs() if j["category"] == demos.IDIOM_CATEGORY]
        self.assertEqual(len(jobs), 6)
        self.assertEqual({j["model"]["id"] for j in jobs}, {0, 1, 2, 3})
        studies = demos.idiom_events()
        for job in jobs:
            with self.subTest(key=job["key"]):
                text = studies[job["key"]]
                self.assertEqual((demos.EVENTS_DIR / f"{job['key']}.events").read_text(), text)
                self.assertTrue(text.startswith("# Protocodus original study"))
                rows = [line.split() for line in text.splitlines() if line and not line.startswith("#")]
                times = [float(row[0]) for row in rows]
                self.assertEqual(times, sorted(times))
                # Every note-off and wheel event precedes the renderer's event cutoff.
                self.assertLess(max(times), job["duration"] - 3)
                self.assertIsNone(job["position"])
                for row in rows:
                    kind, number, value = map(int, row[1:])
                    self.assertIn(kind, (0, 1, 2))
                    self.assertTrue(0 <= value <= 127 and (kind != 2 or number == 1))
        wheel = {j["key"] for j in jobs if " 2 1 " in studies[j["key"]]}
        self.assertEqual(wheel, {"10_Roll_Chorale", "11_Guatemalan_Redoble", "14_Kalimba_Hole_Wah"})
        options = {j["key"]: tuple(j["options"]) for j in jobs}
        self.assertEqual(options["12_Bala_Ostinato"], ("--scale", "1"))
        self.assertEqual(options["13_Amadinda_Interlock"], ("--scale", "3"))
        self.assertEqual(options["15_Dampened_Etude"], ("--note-off", "1"))

    def test_repeated_input_is_fixed_and_retains_three_second_tail(self):
        text = demos.repeated_hit_events()
        self.assertEqual((demos.EVENTS_DIR / f"{demos.REPEATED_STUDY_KEY}.events").read_text(), text)
        rows = [line.split() for line in text.splitlines() if line and not line.startswith("#")]
        self.assertEqual(len(rows), 12)
        self.assertEqual({tuple(row[1:]) for row in rows}, {("1", "60", "90")})
        times = [float(row[0]) for row in rows]
        self.assertEqual(times, [index * 0.5 for index in range(12)])
        self.assertEqual(times[-1] + 3, 8.5)
        self.assertLess(times[-1], 9 - 3)  # All twelve precede the event cutoff.

    def test_both_players_retain_all_repeated_studies(self):
        with tempfile.TemporaryDirectory(prefix="maremba-player-plan-") as directory:
            base = Path(directory)
            audio = base / "Audio"
            audio.mkdir()
            (base / "player_template.html").write_text("const tracks = /*__TRACKS__*/[];")
            results = []
            for job in self.jobs():
                aac = f"{job['key']}_{job['model']['code']}{job['tag']}.m4a"
                (audio / aac).write_bytes(b"codec fixture")
                results.append(dict(song_key=job["key"], category=job["category"], aac=aac))
            with patch.object(demos, "BASE_DIR", base), patch.object(demos, "AUDIO_DIR", audio), \
                    patch.object(demos, "PLAYER", base / "demo-player.html"):
                selected = demos.generate_players(results, {"dsp_sha256": "fixture"})
            self.assertEqual(len(selected), 46)
            self.assertEqual(sum(r["song_key"] == demos.REPEATED_STUDY_KEY for r in selected), 4)
            for path, count in ((base / "demo-player.html", 46), (audio / "demo_player.html", 62)):
                text = path.read_text()
                tracks = json.loads(text.removeprefix("const tracks = ").removesuffix(";"))
                self.assertEqual(len(tracks), count)
                self.assertEqual(sum(t["song_key"] == demos.REPEATED_STUDY_KEY for t in tracks), 4)


class RendererContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        demos.compile_renderer()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="maremba-renderer-test-")
        self.directory = Path(self.temp.name)
        self.events = self.directory / "input.events"
        self.output = self.directory / 'quote"and\\slash.wav'
        self.events.write_text("0 1 60 100\n0.2 0 60 0\n")

    def tearDown(self):
        self.temp.cleanup()

    def run_renderer(self, *args, output=None):
        return subprocess.run([str(demos.RENDERER_BIN), "--events", str(self.events),
                               "--output", str(output or self.output), "--max-duration", "0.4", "--json", *args],
                              capture_output=True, text=True)

    def read_frames(self):
        with wave.open(str(self.output), "rb") as source:
            self.assertEqual((source.getnchannels(), source.getsampwidth(), source.getframerate()), (2, 2, 44100))
            return source.readframes(source.getnframes())

    def test_invalid_arguments_fail_without_writing(self):
        for args in [("--model", "4"), ("--mallet", "-1"), ("--model", "1junk"),
                     ("--max-duration", "nan"), ("--max-duration", "inf"),
                     ("--max-duration", "0"), ("--max-duration", "4garbage"),
                     ("--polyphony", "3"), ("--oversampling", "3"),
                     ("--strike-position", "nan"), ("--strike-position", "1.1"),
                     ("--scale", "7"), ("--scale", "-1"), ("--note-off", "3"),
                     ("--mallet", "6"), ("--carving", "1.5"), ("--tension", "nan"),
                     ("--unknown",), ("--model",)]:
            with self.subTest(args=args):
                first, second = self.run_renderer(*args), self.run_renderer(*args)
                self.assertNotEqual(first.returncode, 0)
                self.assertEqual(first.stderr, second.stderr)
                self.assertFalse(self.output.exists())

    def test_invalid_events_fail_with_line_number(self):
        for row in ["nan 1 60 90", "-0.1 1 60 90", "0 3 60 90", "0 1 128 90", "0 1 60 128",
                    "0 1 60 -1", "0 1 60", "0 1 60 100 extra", "0 2 7 64", "0 2 1 128"]:
            with self.subTest(row=row):
                self.events.write_text("# contract\n" + row + "\n")
                result = self.run_renderer()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid event at line 2", result.stderr)
                self.assertFalse(self.output.exists())

    def test_silent_inputs_and_output_errors_fail(self):
        for text in ["# empty\n", "0 1 60 0\n", "1 1 60 90\n"]:
            self.events.write_text(text)
            result = self.run_renderer()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("No audible note-on", result.stderr)
        self.events.write_text("0 1 60 90\n")
        result = self.run_renderer(output=self.directory / "missing" / "output.wav")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Failed to open output WAV", result.stderr)

    def test_event_rounded_onto_exclusive_endpoint_is_rejected(self):
        for seconds, duration in [("0.399999", "0.4"), ("0.009999", "0.01")]:
            with self.subTest(duration=duration):
                self.events.write_text(f"{seconds} 1 60 100\n")
                result = self.run_renderer("--max-duration", duration)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("No audible note-on", result.stderr)
                self.assertFalse(self.output.exists())

    def test_repeatable_wav_and_json_filename_escape(self):
        first = self.run_renderer()
        self.assertEqual(first.returncode, 0, first.stderr)
        metrics = json.loads(first.stdout)
        self.assertEqual(metrics["output_file"], str(self.output))
        self.assertEqual(metrics["non_finite_samples"], 0)
        digest = hashlib.sha256(self.output.read_bytes()).hexdigest()
        self.assertEqual(self.run_renderer().returncode, 0)
        self.assertEqual(hashlib.sha256(self.output.read_bytes()).hexdigest(), digest)
        demos.verify_wav(self.output)

    def test_controlled_strike_reports_zero_optional_variation(self):
        result = self.run_renderer("--model", "2", "--controlled-strike", "--strike-position", "0.5")
        self.assertEqual(result.returncode, 0, result.stderr)
        metrics = json.loads(result.stdout)
        self.assertTrue(metrics["controlled_strike"])
        self.assertEqual(metrics["strike_jitter"], 0)
        self.assertEqual(metrics["artifacts"], 0)
        self.assertEqual(metrics["strike_position"], 0.5)
        self.assertAlmostEqual(metrics["mallet_hardness"], 0.55, places=5)

    def test_performance_controls_reach_the_engine(self):
        # A held key rolls once the wheel is up; a wheel event is not a note-off.
        self.events.write_text("0 1 60 90\n0.05 2 1 0\n")
        self.assertEqual(self.run_renderer().returncode, 0)
        still = self.read_frames()
        self.events.write_text("0 1 60 90\n0.05 2 1 100\n")
        result = self.run_renderer()
        self.assertEqual(result.returncode, 0, result.stderr)
        metrics = json.loads(result.stdout)
        self.assertEqual((metrics["mod_wheel_events"], metrics["notes_processed"]), (1, 1))
        self.assertNotEqual(self.read_frames(), still)
        # Damp stops a released key; a traditional tuning moves a white key.
        self.events.write_text("0 1 64 100\n0.1 0 64 0\n")
        ring = self.run_renderer()
        ringing = self.read_frames()
        damp = self.run_renderer("--note-off", "1")
        self.assertEqual((ring.returncode, damp.returncode), (0, 0))
        self.assertEqual(json.loads(damp.stdout)["note_off_mode"], 1)
        self.assertNotEqual(self.read_frames(), ringing)
        scaled = self.run_renderer("--scale", "1")
        self.assertEqual(scaled.returncode, 0, scaled.stderr)
        self.assertEqual(json.loads(scaled.stdout)["tuning_scale"], 1)
        self.assertNotEqual(self.read_frames(), ringing)

    def test_exact_frame_onset_matches_shifted_render(self):
        self.events.write_text("0 1 60 100\n")
        self.assertEqual(self.run_renderer().returncode, 0)
        origin = self.read_frames()
        # Non-boundary onsets must move by exactly the requested sample count,
        # rather than jumping backwards to the containing 64-frame block.
        for frame in [1, 31, 63, 64, 65, 127]:
            with self.subTest(frame=frame):
                self.events.write_text(f"{frame / 44100:.12f} 1 60 100\n")
                result = self.run_renderer()
                self.assertEqual(result.returncode, 0, result.stderr)
                delayed = self.read_frames()
                self.assertEqual(delayed[:frame * 4], bytes(frame * 4))
                # Compare before the fixed 10 ms endpoint fade.
                self.assertEqual(delayed[frame * 4:10000 * 4], origin[:(10000 - frame) * 4])

    def test_unsorted_events_stably_match_sorted_render(self):
        ordered = "0 1 60 90\n0.020430839 1 64 90\n0.150340136 1 67 90\n"
        self.events.write_text(ordered)
        self.assertEqual(self.run_renderer().returncode, 0)
        reference = self.output.read_bytes()
        self.events.write_text("   # comments can be indented\n" + "\n".join(reversed(ordered.splitlines())))
        self.assertEqual(self.run_renderer().returncode, 0)
        self.assertEqual(self.output.read_bytes(), reference)


if __name__ == "__main__":
    unittest.main(verbosity=2)
