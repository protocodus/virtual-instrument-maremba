#!/usr/bin/env python3
"""Synthetic sensitivity controls for gross Room temporal artifacts."""
import math
import unittest
import numpy as np
from room_temporal_quality import check_temporal, measure_temporal


class RoomTemporalQualityTest(unittest.TestCase):
    @staticmethod
    def signals(rate=48000):
        rng = np.random.default_rng(6985)
        count = rate
        time = np.arange(count) / rate
        decay = np.exp(-math.log(1000) * time / .8)[:, None]
        noise = rng.standard_normal((count, 2))
        packet = rng.standard_normal((round(.02 * rate), 2))
        repeated = np.tile(packet, (math.ceil(count / len(packet)), 1))[:count]
        gap = noise.copy(); gap[round(.08 * rate):round(.12 * rate)] = 0
        pulses = np.zeros_like(noise)
        pulses[::round(.025 * rate)] = rng.choice([-1., 1.], size=(len(pulses[::round(.025 * rate)]), 2))
        return {name: audio * decay for name, audio in {
            "noise": noise, "repeated": repeated, "mixed": math.sqrt(.8) * repeated + math.sqrt(.2) * noise,
            "gap": gap, "sparse": pulses, "pulsing": noise * (1 + .5 * np.sin(2 * np.pi * 20 * time))[:, None]}.items()}

    def failures(self, audio, rate=48000):
        return check_temporal(measure_temporal(audio, rate, [0, 0]))

    def test_independent_noise_at_all_quality_rates(self):
        for rate in (44100, 48000, 96000):
            with self.subTest(rate=rate):
                self.assertEqual(self.failures(self.signals(rate)["noise"], rate), [])

    def test_repeated_dense_packet_and_independent_noise_mixture(self):
        signals = self.signals()
        for name in ("repeated", "mixed"):
            with self.subTest(name=name):
                measured = measure_temporal(signals[name], 48000, [0, 0])
                self.assertTrue(all(c["low_density_run_ms"] == 0 for c in measured["channels"]))
                self.assertTrue(any("waveform packet" in s for s in check_temporal(measured)))

    def test_density_hole_and_sparse_echoes(self):
        for name in ("gap", "sparse"):
            with self.subTest(name=name):
                self.assertTrue(any("density deficit" in s for s in self.failures(self.signals()[name])))

    def test_periodic_energy_with_uncorrelated_samples(self):
        failures = self.failures(self.signals()["pulsing"])
        self.assertTrue(any("energy envelope" in s for s in failures))
        self.assertFalse(any("waveform packet" in s for s in failures))

    def test_attenuation_cannot_hide_a_repeated_packet(self):
        self.assertTrue(any("waveform packet" in s for s in self.failures(self.signals()["repeated"] * .01)))

    def test_causal_onset_is_not_a_late_density_hole(self):
        audio = self.signals()["noise"]
        delay = round(.048 * 48000)
        shifted = np.vstack((np.zeros((delay, 2)), audio))
        self.assertEqual(check_temporal(measure_temporal(shifted, 48000, [.048, .048])), [])

    def test_nonfinite_short_silent_and_malformed_inputs_are_rejected(self):
        good = self.signals()["noise"]
        invalid = [np.zeros_like(good), good[:1000], good[:, 0], np.zeros((0, 2)), np.c_[good[:, 0], np.zeros(len(good))]]
        early_only = np.zeros_like(good); early_only[0] = 1; invalid.append(early_only)
        for value in (float("nan"), float("inf"), -float("inf")):
            bad = good.copy(); bad[15, 0] = value; invalid.append(bad)
        for audio in invalid:
            with self.subTest(shape=audio.shape):
                with self.assertRaises(ValueError): measure_temporal(audio, 48000, [0, 0])
        for rate in (0, -1, float("nan"), float("inf")):
            with self.assertRaises(ValueError): measure_temporal(good, rate, [0, 0])
        for arrivals in ([0], [-1, 0], [float("nan"), 0], [float("inf"), 0], [2, 2]):
            with self.assertRaises(ValueError): measure_temporal(good, 48000, arrivals)

    def test_invalid_measurements_cannot_nan_pass(self):
        good = measure_temporal(self.signals()["noise"], 48000, [0, 0])
        for bad in ({}, {"channels": []}, {"channels": [None, None]}):
            self.assertTrue(check_temporal(bad))
        for key in good["channels"][0]:
            for value in (float("nan"), float("inf")):
                invalid = {"channels": [dict(good["channels"][0]), dict(good["channels"][1])]}
                invalid["channels"][0][key] = value
                with self.subTest(key=key, value=value):
                    self.assertTrue(check_temporal(invalid))


if __name__ == "__main__":
    unittest.main()
