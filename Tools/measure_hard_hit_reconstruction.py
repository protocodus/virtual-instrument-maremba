#!/usr/bin/env python3
"""Offline 4x reconstruction audit of HardHitOutputTest's unprocessed captures.

This is a reproducible diagnostic, not a certified true-peak measurement or a
proof for arbitrary waveforms. Each capture has seven interleaved float32 outputs.
The native runner retains the worst sample-peak case on each channel at every
sample-rate/quality combination. Thirty-two source frames are excluded at both
excerpt boundaries to avoid interpreting zero-padding an unfinished tail as DSP.
"""
import argparse
import hashlib
import json
import pathlib
import platform

import numpy as np
import scipy
from scipy.signal import resample_poly


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--max-peak", type=float, help="Optional strict gate after saving the complete diagnostic report")
    args = parser.parse_args()
    matrix_path = args.directory / "matrix.json"
    matrix = json.loads(matrix_path.read_text())
    channels = matrix["channels"]
    assert len(channels) == 7
    edge = 32
    records = []
    maxima = np.zeros(7)
    winners = [None] * 7
    for case in matrix["results"]:
        if "audio_file" not in case:
            continue
        path = args.directory / case["audio_file"]
        raw = np.fromfile(path, dtype="<f4").reshape(-1, 7)
        assert raw.shape[0] == case["frames"] and np.isfinite(raw).all()
        actual_peak = np.max(np.abs(raw), axis=0)
        assert np.allclose(actual_peak, case["peak"], atol=1e-10, rtol=1e-10)
        reconstructed = resample_poly(raw.astype(np.float64), 4, 1, axis=0,
                                      window=("kaiser", 8.6))
        interior = np.abs(reconstructed[4 * edge:-4 * edge])
        peak = np.max(interior, axis=0)
        frame = np.argmax(interior, axis=0) / 4 + edge
        for ch in range(7):
            if peak[ch] > maxima[ch]:
                maxima[ch] = peak[ch]
                winners[ch] = {"case_id": case["id"], "source_frame": float(frame[ch]),
                               "sample_rate": case["sample_rate"],
                               "oversampling_factor": case["oversampling_factor"],
                               "model": case["model"], "striker": case["striker"]}
        records.append({
            "case_id": case["id"], "audio_file": case["audio_file"],
            "sha256": sha256(path), "bytes": path.stat().st_size,
            "frames": case["frames"], "sample_rate": case["sample_rate"],
            "sample_peak": actual_peak.tolist(), "reconstructed_peak_4x": peak.tolist(),
            "reconstructed_peak_frame": frame.tolist(),
            "reconstructed_samples_over_full_scale": np.sum(interior > 1., axis=0).tolist(),
            "edge_inclusive_peak_4x": np.max(np.abs(reconstructed), axis=0).tolist(),
        })
    assert records, "matrix contained no retained audio"
    report = {
        "schema": 1,
        "scope": "Worst sample-peak clips per output, sample rate and quality; all seven outputs in every retained clip",
        "method": "scipy.signal.resample_poly(float64, up=4, down=1, axis=0, window=('kaiser', 8.6))",
        "claim_limit": "Offline reconstruction diagnostic; neither certified true peak nor an arbitrary-signal proof",
        "source_frames_excluded_at_each_boundary": edge,
        "tools": {"python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__},
        "matrix": {"file": "matrix.json", "sha256": sha256(matrix_path), "cases": matrix["cases"]},
        "channels": channels, "retained_cases": len(records),
        "required_maximum_peak": args.max_peak,
        "maximum_peak_gate_passed": None if args.max_peak is None else bool(np.all(maxima <= args.max_peak)),
        "maximum_reconstructed_peak_4x": maxima.tolist(),
        "maximum_reconstructed_peak_4x_dbfs": (20 * np.log10(np.maximum(maxima, 1e-30))).tolist(),
        "worst_cases": winners, "results": records,
    }
    output = args.output or args.directory / "reconstruction.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"output": str(output), "retained_cases": len(records),
                      "channels": channels, "maximum_reconstructed_peak_4x": maxima.tolist()}, indent=2))
    if args.max_peak is not None and np.any(maxima > args.max_peak):
        raise SystemExit("FAIL: reconstructed peak exceeds requested maximum")


if __name__ == "__main__":
    main()
