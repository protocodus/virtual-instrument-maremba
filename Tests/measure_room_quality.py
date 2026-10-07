#!/usr/bin/env python3
"""Measure production RoomCapture, without changing or normalizing the DSP.

Requires numpy/scipy. Compiles unchanged source headers into a native probe.
The existing friend diagnostic excludes only the direct tap after AddSource;
six early reflections, both late networks and microphone receivers remain.
All output is wet-only, from the same .25 pressure impulse at the same position.
CPU values describe this native host and are never acceptance thresholds.

    python3 Tests/measure_room_quality.py --output Release/validation/room-quality --check
    python3 Tests/measure_room_quality.py --source-dsp /path/to/frozen/DSP --output /tmp/baseline
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import time

import numpy as np
from scipy import signal
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parent.parent
NAMES = ("Wood", "Chamber", "Hall")
RATES = (44100, 48000, 96000)
BANDS = (125, 500, 4000, 10000)
# Production-room quality bounds leave margin across 44.1/48/96k while rejecting
# the former sparse Hall onset and strongly metallic Wood/Chamber tails.
# They are measured properties, independent of the implementation's loop sizes.
EARLY_ROUGHNESS_MAX = {"Wood": 7.5, "Chamber": 8.5, "Hall": 11.0}
LATE_ROUGHNESS_MAX = {"Wood": 12.5, "Chamber": 10.0, "Hall": 17.0}
# Audible room goals, independent of the implementation's absorption targets:
# compact studio/chamber tails, a longer Hall, and bass that drains before the
# mids while upper mids retain air. Merely matching a too-resonant material
# table must not pass this gate.
MIDBAND_DECAY_RANGE = {"Wood": (.45, .70), "Chamber": (.40, .65), "Hall": (1.50, 2.20)}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decay_fit(energy, rate):
    edc = np.cumsum(energy[::-1])[::-1]
    if not edc[0] > 0:
        raise ValueError("Silent impulse response")
    db = 10 * np.log10(np.maximum(edc / edc[0], 1.e-300))
    indices = np.flatnonzero((db <= -5) & (db >= -35))
    if len(indices) < 3 or db[-1] > -35:
        raise ValueError("Impulse response does not contain a -5 to -35dB decay")
    x, y = indices / rate, db[indices]
    slope, intercept = np.polyfit(x, y, 1)
    r2 = 1 - np.sum((y - (slope*x + intercept))**2) / np.sum((y - np.mean(y))**2)
    return {"seconds": float(-60 / slope), "fit_r2": float(r2)}


def echo_density(samples, rate, start, end):
    # 20ms Hann-weighted fraction above one RMS standard deviation, divided by
    # the Gaussian fraction; .25ms hops. One means Gaussian-like local density.
    half = round(.01 * rate)
    length = 2 * half + 1
    weights = .5 - .5 * np.cos(2*np.pi*(np.arange(length)+1)/(length+1))
    weights /= np.sum(weights)
    values = []
    for center in range(round(start*rate), round(end*rate)+1, max(1, round(.00025*rate))):
        part = samples[center-half:center+half+1]
        rms = np.sqrt(np.sum(weights*part*part))
        values.append(np.sum(weights*(np.abs(part) > rms)) / math.erfc(1/math.sqrt(2)))
    return float(np.mean(values))


def spectrum(samples, rate, start, end):
    # Keep the smoothing bandwidth fixed at +/-500Hz across rates/windows.
    # At 48k and 100-600ms this is the existing legacy test's 501-bin average.
    part = samples[round(start*rate):round(end*rate)]
    bins = np.fft.rfftfreq(len(part), 1/rate)
    db = 20*np.log10(np.maximum(np.abs(np.fft.rfft(part*np.hanning(len(part)))), 1.e-30))
    half = round(500*len(part)/rate)
    indices = np.arange(len(db))
    lo, hi = np.maximum(0, indices-half), np.minimum(len(db), indices+half+1)
    cumulative = np.r_[0, np.cumsum(db)]
    residual = db - (cumulative[hi]-cumulative[lo])/(hi-lo)
    selected = (bins >= 200) & (bins <= 4000)
    peak = np.flatnonzero(selected)[np.argmax(residual[selected])]
    return {"roughness_db": float(np.std(residual[selected])),
            "max_peak_above_smooth_db": float(residual[peak]), "peak_hz": float(bins[peak]),
            "p95_above_smooth_db": float(np.percentile(residual[selected], 95))}


def stereo(samples, rate, start, end):
    part = samples[round(start*rate):round(end*rate)]
    energy = np.sum(part*part, axis=0)
    correlation = np.sum(part[:, 0]*part[:, 1])/np.sqrt(np.prod(energy))
    mono_loss = 10*np.log10(np.mean(np.mean(part, axis=1)**2)/np.mean(part*part))
    return {"correlation": float(correlation), "mono_loss_db": float(mono_loss)}


def tail_balance(samples, rate):
    # Absolute energy is retained alongside the ratio: increasing the highs
    # must not masquerade as removing a pooled low/mid tail. Both measurements
    # use one fixed, unnormalized production impulse and the 100ms-1s window.
    bands = {}
    for name, edges in (("body_100_600hz", (100, 600)), ("air_2000_8000hz", (2000, 8000))):
        filtered = signal.sosfilt(signal.butter(3, edges, btype="bandpass", fs=rate, output="sos"), samples, axis=0)
        energy = np.sum(filtered[round(.1*rate):rate]**2) / (2*rate)
        bands[name+"_energy_db"] = float(10*np.log10(max(energy, 1.e-300)))
    bands["air_to_body_db"] = bands["air_2000_8000hz_energy_db"] - bands["body_100_600hz_energy_db"]
    return bands


def measure(output, metadata, write_wav):
    rate, room = metadata["sample_rate"], metadata["room"]
    stem = output / f"room{room}-{rate}"
    audio = np.fromfile(stem.with_suffix(".f32"), np.float32).reshape(-1, 2).astype(float)
    if audio.shape != (metadata["frames"], 2) or not np.isfinite(audio).all():
        raise ValueError(f"Invalid rendered audio: {stem}")
    result = {"room": NAMES[room], "sample_rate": rate, "metadata": metadata,
              "peak_dbfs": float(20*np.log10(np.max(np.abs(audio)))),
              # Time integral allows meaningful comparisons across sample rates.
              "wet_energy_db": float(10*np.log10(np.sum(audio*audio)/(2*rate))),
              "broadband_t60": decay_fit(np.sum(audio*audio, axis=1), rate),
              "band_t60": {}, "channel": [],
              "tail_balance_100_1000ms": tail_balance(audio, rate),
              "stereo_100_600ms": stereo(audio, rate, .1, .6),
              "stereo_600_2000ms": stereo(audio, rate, .6, 2)}
    for hz in BANDS:
        omega = 2*np.pi*hz/rate
        alpha = np.sin(omega)/(2*4.32)
        a0 = 1+alpha
        section = [alpha/a0, 0, -alpha/a0, 1, -2*np.cos(omega)/a0, (1-alpha)/a0]
        filtered = signal.sosfilt(np.array([section, section]), audio, axis=0)
        result["band_t60"][str(hz)] = decay_fit(np.sum(filtered*filtered, axis=1), rate)
    for channel in range(2):
        late = metadata["late_first_arrival_seconds"][channel]
        part = audio[round((late+.02)*rate):round((late+.08)*rate), channel]
        result["channel"].append({
            "ned_50_150ms": echo_density(audio[:, channel], rate, .05, .15),
            "ned_late_plus20_80ms": echo_density(audio[:, channel], rate, late+.02, late+.08),
            "nonzero_late_plus20_80ms": float(np.mean(part != 0)),
            "spectrum_100_600ms": spectrum(audio[:, channel], rate, .1, .6),
            "spectrum_600_2000ms": spectrum(audio[:, channel], rate, .6, 2)})
    timing = np.genfromtxt(str(stem)+"-cpu.csv", delimiter=",", names=True)["cpu_us"]
    result["native_cpu_observation"] = {"median_us_per_64": float(np.median(timing)),
                                        "p99_us_per_64": float(np.percentile(timing, 99)),
                                        "total_seconds": float(np.sum(timing)*1.e-6),
                                        "not_a_realtime_host_guarantee": True}
    if write_wav:
        wavfile.write(str(stem)+"-wet.wav", rate, audio.astype(np.float32))
    result["raw_audio_sha256"] = digest(stem.with_suffix(".f32"))
    return result


def check(results):
    failures = []
    for result in results:
        where = f'{result["room"]}@{result["sample_rate"]}'
        metadata = result["metadata"]
        for key in ("finite", "repeatable", "silent_after_reset"):
            if not metadata[key]:
                failures.append(f"{where}: {key} failed")
        for hz, target in zip(BANDS, metadata["decay_targets"]):
            fit = result["band_t60"][str(hz)]
            if abs(fit["seconds"]/target-1) > .12 or fit["fit_r2"] < .95:
                failures.append(f"{where}: {hz}Hz decay differs from room absorption target or is nonlinear")
        bass, mid, air = (result["band_t60"][str(hz)]["seconds"] for hz in (125, 500, 4000))
        minimum, maximum = MIDBAND_DECAY_RANGE[result["room"]]
        if not minimum <= mid <= maximum:
            failures.append(f"{where}: midband tail outside voiced room range {minimum}-{maximum}s")
        if bass > .95*mid:
            failures.append(f"{where}: bass tail does not drain before the midband")
        if not .70 <= air/mid <= 1.0:
            failures.append(f"{where}: upper-mid decay does not retain the intended airy balance")
        if result["peak_dbfs"] < -100:
            failures.append(f"{where}: inaudible wet impulse")
        for channel, metrics in enumerate(result["channel"]):
            for key in ("ned_50_150ms", "ned_late_plus20_80ms"):
                if metrics[key] < .9:
                    failures.append(f"{where} channel{channel}: {key} < .9")
            for window, limits in (("100_600ms", EARLY_ROUGHNESS_MAX),
                                   ("600_2000ms", LATE_ROUGHNESS_MAX)):
                roughness = metrics["spectrum_"+window]["roughness_db"]
                if roughness > limits[result["room"]]:
                    failures.append(f"{where} channel{channel}: spectral roughness {window} exceeds room bound")
        for window in ("100_600ms", "600_2000ms"):
            metrics = result["stereo_"+window]
            if abs(metrics["correlation"]) > .35 or metrics["mono_loss_db"] < -5:
                failures.append(f"{where}: stereo correlation or mono energy outside bounds at {window}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dsp", type=Path, default=ROOT/"DSP")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rates", type=int, nargs="+", choices=RATES, default=list(RATES))
    parser.add_argument("--baseline", type=Path, help="Earlier metrics.json; descriptive deltas only")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--wav", action="store_true", help="Keep unnormalized float32 wet listening files")
    parser.add_argument("--tone-sweep", action="store_true", help="Measure fixed sine bursts at every semitone from MIDI 36 to 96")
    args = parser.parse_args()
    started = time.monotonic()
    source, output = args.source_dsp.resolve(), args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error(f"Output must be empty so earlier evidence is preserved: {output}")
    output.mkdir(parents=True, exist_ok=True)
    probe = ROOT/"Tests/room_quality_probe.cpp"
    command = [os.environ.get("CXX", "clang++"), "-std=c++17", "-O3", "-Wall", "-Wextra", "-Werror",
               "-I"+str(source), str(probe), "-o", str(output/"room-quality-probe")]
    metadata = {"source_dsp": str(source), "source_hashes": {str(p.relative_to(source)): digest(p) for p in sorted(source.rglob("*.h"))},
                "runner_sha256": digest(Path(__file__)), "probe_sha256": digest(probe), "compile_command": command,
                "normalization": "None; fixed .25 pressure impulse, wet-only receiver output",
                "quality_scope": "Host-rate RoomCapture. Mechanical 2x/4x/8x quality is upstream and excluded."}
    (output/"source-inputs.json").write_text(json.dumps(metadata, indent=2)+"\n")
    with (output/"compile.log").open("w") as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    results = []
    for rate in args.rates:
        for room in range(3):
            rendered = subprocess.run([str(output/"room-quality-probe"), str(output), str(rate), str(room)],
                                      check=True, capture_output=True, text=True)
            item = measure(output, json.loads(rendered.stdout), args.wav)
            if args.tone_sweep:
                tones = subprocess.run([str(output/"room-quality-probe"), str(output), str(rate), str(room), "--tones"],
                                       check=True, capture_output=True, text=True)
                item["tonal_response"] = json.loads(tones.stdout)
            results.append(item)
            print(f'{NAMES[room]} @ {rate}: RT60={item["broadband_t60"]["seconds"]:.3f}s '
                  f'NED={[round(c["ned_50_150ms"], 3) for c in item["channel"]]} '
                  f'roughness={[round(c["spectrum_100_600ms"]["roughness_db"], 2) for c in item["channel"]]}', flush=True)
    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        lookup = {(r["room"], r["sample_rate"]): r for r in baseline["results"]}
        for result in results:
            before = lookup[(result["room"], result["sample_rate"])]
            result["baseline_delta"] = {"wet_energy_db": result["wet_energy_db"]-before["wet_energy_db"],
                                         "broadband_t60_ratio": result["broadband_t60"]["seconds"]/before["broadband_t60"]["seconds"]}
    failures = check(results) if args.check else []
    report = {"results": results, "checked": args.check, "passed": not failures if args.check else None,
              "failures": failures, "elapsed_seconds": time.monotonic()-started}
    (output/"metrics.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    for failure in failures:
        print("FAIL "+failure)
    print(f'Report: {output/"metrics.json"} ({report["elapsed_seconds"]:.1f}s)', flush=True)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
