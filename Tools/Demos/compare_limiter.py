#!/usr/bin/env python3
"""Separate old-compressor/new-limiter auditions; existing demos are untouched.

python3 Tools/Demos/compare_limiter.py --prepare-baseline  # optional cached old renders
python3 Tools/Demos/compare_limiter.py               # final DSP, reports and audio
Requires clang++ and, for final delivery, ffmpeg. No loudness matching or boost.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import wave

DEMOS = Path(__file__).resolve().parent
ROOT = DEMOS.parents[1]
OUT = DEMOS / "Audio" / "limiter_comparison"
HARNESS = DEMOS / "render_limiter_comparison.cpp"
PIN = ROOT / "Docs/validation/limiter-baseline-source-sha256.txt"
# The pin records Rack commit 7b00172; this is the same engine tree in this
# repository's history, where Examples/Maremba/DSP became Source/DSP.
BASELINE = "3d597b690fb8db6291060fb8227644a87f9ea912"
FLAGS = ["-std=c++17", "-O3", "-Wall", "-Wextra", "-Werror"]
PROGRAMS = ("factory_like", "driven")
AMOUNTS = (0.0, 0.25, 1.0)
RATE, FRAMES, CHANNELS = 44100, 253575, 7
CHANNEL_NAMES = ("main_left", "main_right", "close_left", "close_right", "room_left", "room_right", "piezo")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dsp_dir(root):
    """The baseline cache keeps DSP/ at its root; the current tree keeps Source/DSP."""
    return root / "DSP" if (root / "DSP").is_dir() else root / "Source/DSP"


def hashes(root):
    return {str(p.relative_to(root)): sha(p) for p in sorted(dsp_dir(root).iterdir())
            if p.suffix in (".cpp", ".h")}


def baseline_source():
    """Reconstruct only pinned source bytes, without a worktree or git mutation."""
    source = OUT / "baseline_source"
    expected = {}
    for line in PIN.read_text().splitlines():
        if not line.startswith("#"):
            digest, relative = line.split("  ", 1)
            if relative.startswith("DSP/"):
                expected[relative] = digest
    if not expected:
        raise RuntimeError("Baseline DSP hash list is empty")
    for relative, digest in expected.items():
        data = subprocess.check_output(["git", "show", f"{BASELINE}:Source/{relative}"], cwd=ROOT)
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f"Pinned baseline source mismatch: {relative}")
        target = source / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    if hashes(source) != expected:
        raise RuntimeError("Unexpected files or hashes in baseline DSP cache")
    return source


def build(source, binary):
    command = ["clang++", *FLAGS, "-I", str(dsp_dir(source)), str(HARNESS),
               *map(str, sorted(dsp_dir(source).glob("*.cpp"))), "-o", str(binary)]
    subprocess.run(command, check=True)


def samples(path):
    data = array.array("f")
    data.frombytes(path.read_bytes())
    if sys.byteorder != "little":
        data.byteswap()
    if len(data) != FRAMES * CHANNELS or any(not math.isfinite(x) for x in data):
        raise RuntimeError(f"Invalid/non-finite raw audio: {path}")
    return [data[ch::CHANNELS] for ch in range(CHANNELS)]


def db(value):
    return 20 * math.log10(max(value, 1e-30))


def main_stats(channels, start=0, end=FRAMES):
    left, right = channels[0][start:end], channels[1][start:end]
    peak = max(max(map(abs, left)), max(map(abs, right)))
    rms = math.sqrt((sum(x*x for x in left) + sum(x*x for x in right)) / (2 * len(left)))
    return {"peak": peak, "peak_dbfs": db(peak), "rms": rms, "rms_dbfs": db(rms),
            "crest_db": db(peak / rms) if rms else None,
            "samples_above_full_scale": sum(abs(x) > 1 for c in (left, right) for x in c),
            "samples_above_0_98": sum(abs(x) > 0.980001 for c in (left, right) for x in c)}


def render_version(source, name):
    binary = OUT / f"{name}_renderer"
    source_before = hashes(source)
    build(source, binary)
    rows, audio = [], {}
    for program in PROGRAMS:
        for amount in AMOUNTS:
            key = f"{program}_{amount:g}"
            path = OUT / f"{name}_{key}.f32"
            result = subprocess.run([str(binary), program, str(amount), str(path)], capture_output=True, text=True)
            (OUT / f"{name}_{key}.log").write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"Renderer failed; raw log retained: {name}_{key}.log")
            metadata = json.loads(result.stdout)
            if (metadata["sample_rate"], metadata["frames"], metadata["channels"]) != (RATE, FRAMES, CHANNELS):
                raise RuntimeError("Renderer output contract changed")
            channels = samples(path)
            audio[key] = channels
            stats = main_stats(channels)
            if stats["rms"] == 0:
                raise RuntimeError(f"Silent output; cannot create an audition: {path}")
            onset = next(i for i, pair in enumerate(zip(channels[0], channels[1])) if pair != (0.0, 0.0))
            rows.append({**metadata, "key": key, "raw_file": path.name, "raw_sha256": sha(path),
                         "main": stats, "first_nonzero_frame": onset,
                         "channel_sha256": {label: hashlib.sha256(c.tobytes()).hexdigest()
                                            for label, c in zip(CHANNEL_NAMES, channels)},
                         "post_accent_windows": {label: main_stats(channels, round(start*RATE), round(end*RATE))
                             for label, start, end in (("early", 2.45, 2.70), ("middle", 3.10, 3.35), ("late", 4.25, 4.50))}})
            print(f"{name} {key}: raw peak {stats['peak_dbfs']:.2f} dBFS, RMS {stats['rms_dbfs']:.2f} dBFS", flush=True)
    if hashes(source) != source_before:
        raise RuntimeError("DSP changed during rendering; raw files retained, no report published")
    record = {"source_sha256": source_before, "binary_sha256": sha(binary), "rows": rows}
    (OUT / f"{name}_raw_measurements.json").write_text(json.dumps(record, indent=2) + "\n")
    return record, audio


def audition(before, after, gain, path):
    """One gain for every old/new/amount render of this program; never boost."""
    stereo = array.array("h")
    fade_frames = round(0.040 * RATE)
    for index, channels in enumerate((before, after)):
        if index:
            stereo.extend([0] * RATE)  # half a second, stereo
        for frame, (left, right) in enumerate(zip(channels[0], channels[1])):
            fade = min(1.0, (FRAMES - 1 - frame) / (fade_frames - 1))
            for value in (left, right):
                scaled = value * gain * fade
                if abs(scaled) >= 1:
                    raise RuntimeError("Shared playback gain would clip")
                stereo.append(round(scaled * 32767))
    if sys.byteorder != "little":
        stereo.byteswap()
    with wave.open(str(path), "wb") as output:
        output.setparams((2, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(stereo.tobytes())
    aac = path.with_suffix(".m4a")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(path),
                    "-map_metadata", "-1", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", str(aac)], check=True)
    decoded = subprocess.check_output(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(aac),
                                       "-f", "f32le", "-acodec", "pcm_f32le", "-"])
    floats = array.array("f"); floats.frombytes(decoded)
    if sys.byteorder != "little":
        floats.byteswap()
    if not floats or any(not math.isfinite(x) for x in floats):
        raise RuntimeError("AAC decode is empty/non-finite")
    codec_peak = max(map(abs, floats))
    if not 0 < codec_peak < 1:
        raise RuntimeError(f"Silent or clipped AAC: {aac}")
    return {"wav": str(path.relative_to(DEMOS)), "aac": str(aac.relative_to(DEMOS)),
            "wav_sha256": sha(path), "aac_sha256": sha(aac), "duration_seconds": 12,
            "common_gain": gain, "common_gain_db": db(gain), "aac_decoded_peak": codec_peak}


def write_report(report):
    text = ["# Limiter listening comparison", "",
            "These separate auditions compare the former gentle compressor with the new limiter. "
            "They use the same original note phrase, parameters and deterministic start. "
            "Each file plays **old for 5.75 seconds, silence for 0.5 seconds, then new for 5.75 seconds**.", "",
            "Amount 0.25 represents the factory amount; Amount 1 demonstrates strong limiting. "
            "Both use Attack 12 ms and Release 120 ms. Factory-like uses a near-Concert-Grand Rosewood program; "
            "Driven uses a harder striker, all microphone levels at 1, Drive 0.75 and Volume 1. "
            "The four single notes rise from MIDI velocity 32 to 127, followed by forte chords and soft notes.", "",
            "Raw measurements below precede PCM conversion, playback gain and the final 40 ms presentation fade. "
            "There is no makeup gain or loudness matching. A single attenuation, never a boost, is shared by "
            "both versions and both amounts within each program; it also allows headroom for its dry reference. "
            "The existing acoustic demo collection is unchanged.", "",
            "| Program | Amount | Old peak | New peak | Old RMS | New RMS | Playback gain | Listen |",
            "|---|---:|---:|---:|---:|---:|---:|---|"]
    old = {r["key"]: r for r in report["baseline"]["rows"]}
    new = {r["key"]: r for r in report["current"]["rows"]}
    for item in report["auditions"]:
        key = item["key"]; a, b = old[key]["main"], new[key]["main"]
        text.append(f"| {item['program']} | {item['amount']:g} | {a['peak_dbfs']:.2f} dBFS | {b['peak_dbfs']:.2f} dBFS | "
                    f"{a['rms_dbfs']:.2f} dBFS | {b['rms_dbfs']:.2f} dBFS | {item['common_gain_db']:.2f} dB | [AAC](../../Tools/Demos/{item['aac']}) · [WAV](../../Tools/Demos/{item['wav']}) |")
    text += ["", "Exact comparisons use unshifted float samples: Amount 0 reproduces all seven outputs "
             "bit-for-bit; every direct stem is identical across versions and amounts. All processed onsets begin "
             "on the same sample as their dry reference. This is a sample-alignment check on these phrases, "
             "not a frequency-dependent group-delay measurement.", "",
             "Post-accent window RMS ratios in the JSON include ringing tails and following notes; they illustrate "
             "recovery in music, not an isolated measurement of the detector's release constant. Dedicated limiter "
             "tests establish ceiling and ballistics contracts. A sample-peak bound does not establish true-peak limiting.", "",
             f"Baseline DSP: `{BASELINE}`. All source, binary, raw-audio and delivery hashes are in "
             "[the machine-readable report](limiter-comparison.json). Audio and compiler caches are local, ignored files.", "",
             "Reproduce from Maremba: `python3 Tools/Demos/compare_limiter.py`. Requires clang++ and ffmpeg. "
             "`--prepare-baseline` captures only the hash-verified old engine renders.", ""]
    (ROOT / "Docs/validation/limiter-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    (ROOT / "Docs/validation/limiter-comparison.md").write_text("\n".join(text))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-baseline", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    before_source = baseline_source()
    before, old_audio = render_version(before_source, "baseline")
    if args.prepare_baseline:
        return
    if not shutil.which("ffmpeg"):
        raise RuntimeError("ffmpeg is required for final auditions")
    harness_hash = sha(HARNESS)
    after, new_audio = render_version(ROOT, "current")
    checks, auditions = [], []
    for program in PROGRAMS:
        dry = f"{program}_0"
        if any(a.tobytes() != b.tobytes() for a, b in zip(old_audio[dry], new_audio[dry])):
            raise RuntimeError(f"Amount 0 is not bit-identical without alignment: {program}")
        rows = [(record, row) for record in (before, after) for row in record["rows"] if row["program"] == program]
        onset = next(row["first_nonzero_frame"] for row in before["rows"] if row["key"] == dry)
        for record, row in rows:
            audio = old_audio if record is before else new_audio
            if any(audio[row["key"]][ch].tobytes() != old_audio[dry][ch].tobytes() for ch in range(2, 7)):
                raise RuntimeError(f"Direct stem changed: {program}/{row['amount']}")
            if row["first_nonzero_frame"] != onset:
                raise RuntimeError(f"Added onset delay: {program}/{row['amount']}")
            dry_row = next(r for r in record["rows"] if r["key"] == dry)
            for label, window in row["post_accent_windows"].items():
                window["rms_db_relative_to_dry"] = db(window["rms"] / dry_row["post_accent_windows"][label]["rms"])
        checks.append({"program": program, "amount_zero_all_seven_channels_bit_identical": True,
                       "direct_stems_bit_identical_all_versions_and_amounts": True,
                       "processed_onset_matches_dry_sample": True, "first_nonzero_frame": onset,
                       "comparison_alignment_offset_samples": 0})
        gain = min(1.0, math.pow(10, -3 / 20) / max(row["main"]["peak"] for _, row in rows))
        for amount in AMOUNTS[1:]:
            key = f"{program}_{amount:g}"
            item = audition(old_audio[key], new_audio[key], gain, OUT / f"{key}_before_after.wav")
            auditions.append({"key": key, "program": program, "amount": amount, **item})
    if hashes(ROOT) != after["source_sha256"] or sha(HARNESS) != harness_hash:
        raise RuntimeError("Source changed before completion; raw results retained, report not published")
    report = {"schema_version": 1, "baseline_revision": BASELINE,
              "purpose": "Main-output musical comparison; raw measurements precede any playback attenuation.",
              "inputs_sha256": {str(p.relative_to(ROOT)): sha(p) for p in (HARNESS, Path(__file__), PIN)},
              "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
              "compiler_flags": FLAGS, "encoder": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
              "baseline": before, "current": after, "checks": checks, "auditions": auditions}
    write_report(report)
    print("Verified dry sample identity, five direct stems, onset alignment and four 12-second WAV/AAC auditions.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "failure.txt").write_text(f"{type(error).__name__}: {error}\n")
        raise
