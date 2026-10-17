#!/usr/bin/env python3
"""Probe actual OutputHeadroom on 315 pulses, fractional tones and compound bursts.

Requires NumPy, SciPy and a C++17 compiler. Writes into a NEW output directory.
Every case feeds all four protection groups; the C++ transport asserts their
equal responses and every channel's finite, <=.940002 sampled output. The saved
main-L signal is reconstructed independently at 16x and 32x with SciPy's default
length Kaiser8.6 windowed sinc. This is a finite adversarial regression, not a
certified true-peak measurement or a guarantee for arbitrary reconstruction.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

import numpy as np
import scipy
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("Source/DSP/OutputHeadroom.h", "Source/DSP/OutputPeakKernel.h",
           "Tests/OutputHeadroomReconstructionProbe.cpp",
           "Tests/generate_output_peak_kernel.py", "Tests/measure_output_headroom.py")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signals():
    frames = 4096
    cases, waves = [], []

    def add(name, x, **parameters):
        cases.append({"name": name, **parameters})
        waves.append(x.astype("<f4"))

    for amplitude in (.5, .9, .94, 2., 1000.):
        for length in (1, 2, 3, 4, 8, 16, 32):
            x = np.zeros(frames)
            x[512:512 + length] = amplitude
            add(f"plateau_{length}_amplitude_{amplitude}", x,
                amplitude=amplitude, length=length)
        for length in (2, 3, 4, 8):
            x = np.zeros(frames)
            x[512:512 + length] = amplitude * (-1.) ** np.arange(length)
            add(f"alternating_{length}_amplitude_{amplitude}", x,
                amplitude=amplitude, length=length)
    for fraction in (.02, .12345, .25 + .3 / 48000, .499 - .3 / 48000):
        for phase in (0, .3, .7, 1.1, 1.57):
            x = np.zeros(frames)
            t = np.arange(2048)
            x[512:2560] = (2 * np.sin(2 * np.pi * fraction * t + phase)
                          * np.minimum(1, t / 64) * np.minimum(1, (2047 - t) / 64))
            add(f"tone_{fraction}_phase_{phase}", x,
                cycles_per_sample=fraction, phase=phase)
    rng = np.random.default_rng(73896)
    for index in range(240):
        t = np.arange(512)
        number = int(rng.integers(2, 12))
        frequency = rng.uniform(.01, .45, number)
        phase = rng.uniform(0, 2 * np.pi, number)
        amplitude = rng.uniform(.2, 1, number)
        y = (np.sum(amplitude[:, None] * np.sin(2 * np.pi * frequency[:, None]
                                               * t[None, :] + phase[:, None]), axis=0)
             * np.sin(np.pi * t / (len(t) - 1)) ** 2)
        x = np.zeros(frames)
        target_peak = (.9, 2, 8)[index % 3]
        x[512:1024] = y / np.max(np.abs(y)) * target_peak
        add(f"windowed_multitone_{index}", x, input_sample_peak=target_peak,
            cycles_per_sample=frequency.tolist(), phase=phase.tolist(), amplitude=amplitude.tolist())
    assert len(cases) == 315
    return cases, np.array(waves, dtype="<f4")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--rates", type=int, nargs="+", default=[48000])
    parser.add_argument("--max-reconstructed-peak", type=float, default=1.)
    args = parser.parse_args()
    if any(r < 22050 or r > 192000 for r in args.rates):
        parser.error("rates must be within 22050..192000")
    if not np.isfinite(args.max_reconstructed_peak) or args.max_reconstructed_peak <= 0:
        parser.error("maximum reconstructed peak must be positive and finite")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    hashes = {p: sha256(ROOT / p) for p in SOURCES}
    snapshot = output / "source"
    snapshot.mkdir()
    for path in SOURCES:
        shutil.copy2(ROOT / path, snapshot / Path(path).name)
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise SystemExit(f"Compiler not found: {args.compiler}")
    executable = output / "process"
    compile_command = [compiler, "-std=c++17", "-O3", "-Wall", "-Wextra", "-Werror",
                       "-I", str(snapshot), str(snapshot / "OutputHeadroomReconstructionProbe.cpp"),
                       "-o", str(executable)]
    subprocess.run(compile_command, check=True)
    cases, waves = signals()
    input_path = output / "input.f32"
    waves.tofile(input_path)
    (output / "cases.json").write_text(json.dumps({"frames": waves.shape[1], "cases": cases}, indent=2) + "\n")
    rows, commands, summaries = [], [], []
    for rate in args.rates:
        raw_path = output / f"output-{rate}.f32"
        command = [str(executable), str(input_path), str(raw_path), str(waves.shape[1]), str(rate)]
        commands.append(command)
        subprocess.run(command, check=True)
        rendered = np.fromfile(raw_path, dtype="<f4").reshape(waves.shape)
        assert np.isfinite(rendered).all()
        current = []
        for case, original, audio in zip(cases, waves, rendered):
            row = {"rate": rate, "case": case["name"],
                   "input_sample_peak": float(np.max(np.abs(original))),
                   "output_sample_peak": float(np.max(np.abs(audio)))}
            for factor in (16, 32):
                reconstructed = resample_poly(audio.astype(np.float64), factor, 1,
                                              window=("kaiser", 8.6))
                assert np.isfinite(reconstructed).all()
                row[f"reconstructed_{factor}x_peak"] = float(np.max(np.abs(reconstructed)))
            current.append(row)
        rows.extend(current)
        worst = max(current, key=lambda r: r["reconstructed_32x_peak"])
        summary = {"rate": rate, "cases": len(current),
                   "maximum_sample_peak": max(r["output_sample_peak"] for r in current),
                   "maximum_reconstructed_16x_peak": max(r["reconstructed_16x_peak"] for r in current),
                   "maximum_reconstructed_32x_peak": worst["reconstructed_32x_peak"],
                   "worst_32x_case": worst["case"], "output_sha256": sha256(raw_path)}
        summaries.append(summary)
        print(json.dumps(summary), flush=True)
    with (output / "results.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    unchanged = hashes == {p: sha256(ROOT / p) for p in SOURCES}
    passed = all(r["output_sample_peak"] <= .940002
                 and max(r["reconstructed_16x_peak"], r["reconstructed_32x_peak"])
                 <= args.max_reconstructed_peak for r in rows)
    report = {"schema": 1, "scope": __doc__, "source_sha256": hashes,
              "source_unchanged": unchanged,
              "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "invocation": sys.argv, "compiler": compiler,
              "compiler_version": subprocess.check_output([compiler, "--version"], text=True),
              "compile_command": compile_command, "processor_commands": commands,
              "tools": {"python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__},
              "input_sha256": sha256(input_path), "summaries": summaries,
              "required_maximum_reconstructed_peak": args.max_reconstructed_peak,
              "sampled_and_reconstructed_peak_gate_passed": passed,
              "four_groups_equal_and_finite_sample_ceiling_checked": True}
    (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    if not unchanged:
        raise SystemExit("FAIL: production or probe source changed during measurement")
    if not passed:
        raise SystemExit("FAIL: output exceeded the sampled or reconstructed peak gate")
    print("PASS: all synthetic cases and four gain groups")


if __name__ == "__main__":
    main()
