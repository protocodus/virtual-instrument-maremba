#!/usr/bin/env python3
"""Reproduce controlled touch before/after renders against main revision 5438b10.

With --capture-baseline, save pinned source, renderer and 48 original C4/C6 WAV
probes under ignored Audio/TouchBaseline. With no flag, preserve that baseline,
render the current source, and produce Docs/research/touch-comparison.md/json plus local A/B
WAV/AAC auditions. Requires clang++; final A/B encoding also requires ffmpeg.
"""
import argparse
import array
import hashlib
import json
import math
import subprocess
import sys
import wave
from pathlib import Path

import run_benchmark as demos

# Rack main 5438b10 is this repository's 5571c4f (Examples/Maremba/DSP became
# Source/DSP). Its demo renderer was never part of this history, so its bytes are
# kept in Baselines/.
BASELINE_REVISION = "5571c4f810ed22133fe091681e95144c1d55648d"
BASELINE_RENDERER = demos.BASE_DIR / "Baselines" / "touch-5438b10-maremba_renderer.cpp"
BASELINE_DIR = demos.AUDIO_DIR / "TouchBaseline"
CURRENT_DIR = demos.AUDIO_DIR / "TouchCurrent"
NAMES = ["Rosewood", "Padauk", "Balafon", "Kalimba"]
VELOCITIES = [16, 32, 64, 96, 127]


def probe_specs():
    for model in range(4):
        for velocity in VELOCITIES:
            yield dict(name=f"m{model}_velocity{velocity}", model=model, mallet=1, position=0.5, velocity=velocity, note=60)
        for position in ([0.1, 0.35, 0.5, 0.9] if model == 3 else [0.1, 0.2242, 0.35, 0.5]):
            yield dict(name=f"m{model}_position{position:g}", model=model, mallet=1, position=position, velocity=90, note=60)
    for mallet in range(4):
        yield dict(name=f"m3_pluck{mallet}", model=3, mallet=mallet, position=0.5, velocity=90, note=60)
    for model in range(4):
        for velocity in [16, 127]:
            yield dict(name=f"m{model}_C6_velocity{velocity}", model=model, mallet=1, position=0.5, velocity=velocity, note=84)


def source_hashes(directory):
    files = [*sorted(demos.DSP_DIR.glob("*.h")), *sorted(demos.DSP_DIR.glob("*.cpp")),
             directory / "Tools/Demos/maremba_renderer.cpp"]
    return {str(p.relative_to(directory)): demos.sha256(p) for p in files}


def capture_baseline():
    repository = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], cwd=demos.PROJECT_DIR, text=True).strip())
    source = BASELINE_DIR / "source"
    paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", BASELINE_REVISION,
                                    "Source/DSP"],
                                   cwd=repository, text=True).splitlines()
    if not paths:
        raise RuntimeError("Baseline commit/source unavailable in this checkout")
    hashes = {}
    for name in paths:
        data = subprocess.check_output(["git", "show", f"{BASELINE_REVISION}:{name}"], cwd=repository)
        relative = Path(name).relative_to("Source")
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        hashes[str(relative)] = hashlib.sha256(data).hexdigest()
    renderer = source / "Demos/maremba_renderer.cpp"
    renderer.parent.mkdir(parents=True, exist_ok=True)
    renderer.write_bytes(BASELINE_RENDERER.read_bytes())
    hashes["Demos/maremba_renderer.cpp"] = demos.sha256(renderer)
    (BASELINE_DIR / "source_manifest.json").write_text(json.dumps({"revision": BASELINE_REVISION, "source_sha256": hashes}, indent=2) + "\n")
    binary = BASELINE_DIR / "baseline_renderer"
    subprocess.run(["clang++", "-std=c++17", "-O3", "-I", str(source),
                    *map(str, sorted((source / "DSP").glob("*.cpp"))), str(source / "Demos/maremba_renderer.cpp"),
                    "-o", str(binary)], check=True)
    probes = render_probes(binary, BASELINE_DIR)
    result = {"revision": BASELINE_REVISION, "renderer_sha256": demos.sha256(binary), "probes": probes}
    (BASELINE_DIR / "baseline_probes.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def render_probes(binary, output, specs=None):
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for spec in (probe_specs() if specs is None else specs):
        events = output / (spec["name"] + ".events")
        events.write_text(f"0.050000000 1 {spec['note']} {spec['velocity']}\n")
        wav = output / (spec["name"] + ".wav")
        result = subprocess.run([str(binary), "--events", str(events), "--output", str(wav),
                                 "--model", str(spec["model"]), "--mallet", str(spec["mallet"]),
                                 "--strike-position", str(spec["position"]), "--controlled-strike",
                                 "--max-duration", "3", "--json"], check=True, capture_output=True, text=True)
        metrics = json.loads(result.stdout)
        demos.verify_wav(wav)
        results.append({**spec, "metrics": metrics, "wav_sha256": demos.sha256(wav)})
    return results


def wave_samples(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (2, 2, 44100):
            raise RuntimeError(f"Unexpected comparison PCM format: {path}")
        raw = source.readframes(source.getnframes())
    data = array.array("h", raw)
    if sys.byteorder != "little":
        data.byteswap()
    return data, raw


def rms_db(samples, start, end):
    segment = samples[round(start * 88200):round(end * 88200)]
    energy = sum((sample / 32768) ** 2 for sample in segment) / len(segment)
    return 10 * math.log10(max(energy, 1e-16))


def main_bus_metrics(path):
    samples, _ = wave_samples(path)
    # The event begins at 50 ms; retain the full oversampler/microphone response.
    return {"attack_0_20ms_dbfs": rms_db(samples, 0.050, 0.070),
            "body_20_200ms_dbfs": rms_db(samples, 0.070, 0.250),
            "tail_600_1500ms_dbfs": rms_db(samples, 0.650, 1.550)}


def ab_audition(name, probe_names):
    output = CURRENT_DIR / (name + "_before_after.wav")
    parts = []
    for source in [BASELINE_DIR, CURRENT_DIR]:
        for key in probe_names:
            _, raw = wave_samples(source / (key + ".wav"))
            parts.append(raw)
        parts.append(bytes(44100 * 4))  # One second between and after versions.
    with wave.open(str(output), "wb") as sink:
        sink.setnchannels(2)
        sink.setsampwidth(2)
        sink.setframerate(44100)
        sink.writeframes(b"".join(parts))
    aac, metrics = demos.encode_aac(output)
    return {"wav": output.name, "aac": aac.name, "wav_sha256": demos.sha256(output),
            "aac_sha256": demos.sha256(aac), **metrics}


def write_report(result):
    by_name = {row["name"]: row for row in result["probes"]}
    lines = ["# Maremba controlled touch comparison", "",
             f"Baseline: main `{BASELINE_REVISION}`. Current source SHA-256 map is in [touch-comparison.json](touch-comparison.json).", "",
             "Reproduce with `python3 Tools/Demos/compare_touch.py`. The script restores and builds the pinned baseline "
             "when needed; `--capture-baseline` only captures it. All source snapshots, binaries and probe/A-B audio live "
             "in ignored `Tools/Demos/Audio/TouchBaseline` and `Tools/Demos/Audio/TouchCurrent`.", "",
             "Forty-eight three-second single-note C4/C6 main-bus renders per version use identical note timing, gain, model, "
             "striker and position. Strikes start at 50 ms; Variance and Artifacts are zero. "
             "The current engine retains its subtle intrinsic strike variation at these settings. "
             "Compression remains off, while each model retains its documented demo microphone/body settings. "
             "Current demo presets use Buzz 0% and Drive 1%; the pinned baseline uses its original buzz settings and Drive 0%. "
             "These comparisons therefore include changes to demo voicing as well as the engine. "
             "These are end-to-end listening probes; isolated raw-voice tests (the Rack device's Docs/research/touch-response.md) provide separate spectral evidence.", "",
             "RMS uses both stereo channels. Attack is the first 20 ms after the note event, body is 20–200 ms, "
             "and late decay is 600–1500 ms. Measurements include the oversampler and microphone delays. "
             "Comparisons use exported PCM without loudness matching. Every export gain is recorded so any safety "
             "attenuation remains visible. Measured differences establish changed response, not perceptual equivalence to an acoustic instrument.", "",
             "## Quiet-to-loud body dynamic span", "",
             "The span is body RMS at velocity 127 minus velocity 16, at centre position with the medium/natural striker.", "",
             "| Model | Baseline span | Current span | Current pp body | Current ff body |",
             "|---|---:|---:|---:|---:|"]
    for model in range(4):
        pp, ff = by_name[f"m{model}_velocity16"], by_name[f"m{model}_velocity127"]
        body = "body_20_200ms_dbfs"
        old_span = ff["before"][body] - pp["before"][body]
        new_span = ff["after"][body] - pp["after"][body]
        lines.append(f"| {NAMES[model]} | {old_span:.2f} dB | {new_span:.2f} dB | {pp['after'][body]:.2f} dBFS | {ff['after'][body]:.2f} dBFS |")
    lines += ["", "## C6 register comparison", "",
              "Same controls and velocities 16/127, two octaves above C4. The separate raw-voice report measures "
              "spectral/contact changes; these values document the delivered main-bus energy.", "",
              "| Model | Baseline pp / ff body | Current pp / ff body | Current span |",
              "|---|---:|---:|---:|"]
    for model in range(4):
        pp, ff = by_name[f"m{model}_C6_velocity16"], by_name[f"m{model}_C6_velocity127"]
        body = "body_20_200ms_dbfs"
        lines.append(f"| {NAMES[model]} | {pp['before'][body]:.2f} / {ff['before'][body]:.2f} dBFS | "
                     f"{pp['after'][body]:.2f} / {ff['after'][body]:.2f} dBFS | {ff['after'][body] - pp['after'][body]:.2f} dB |")
    lines += ["", "## Position and pluck differences", "",
              "Fixed velocity 90. Negative differences indicate lower energy in that window, not lower quality. "
              "A quieter bar fundamental near its node is an intended spatial behavior. Kalimba percentages are control positions along "
              "the playable end, mapped to 65–100% of physical tine length, not the whole clamp-to-tip distance.", "",
              "| Model / control | Attack change | Body change | Late-decay change |",
              "|---|---:|---:|---:|"]
    for row in result["probes"]:
        if "velocity" in row["name"]:
            continue
        label = f"pluck {row['mallet']}" if "pluck" in row["name"] else f"position {row['position'] * 100:g}%"
        changes = [row["after"][key] - row["before"][key] for key in
                   ["attack_0_20ms_dbfs", "body_20_200ms_dbfs", "tail_600_1500ms_dbfs"]]
        lines.append(f"| {NAMES[row['model']]} / {label} | {changes[0]:+.2f} dB | {changes[1]:+.2f} dB | {changes[2]:+.2f} dB |")
    lines += ["", "## Local A/B auditions", "",
              "Each file plays its probes from the baseline, one second of silence, then the current version "
              "in the same order. Dynamics use velocities 16, 32, 64, 96, 127; Kalimba plucks use flesh, natural thumb, thumbnail and pick. "
              "Register files play C4 pp, C4 ff, C6 pp, C6 ff. Each hit has three seconds to ring. No gain matching is applied. "
              "The following files are generated locally and are not committed:", ""]
    for audio in result["ab_auditions"]:
        lines.append(f"- [{audio['aac']}](../../Tools/Demos/Audio/TouchCurrent/{audio['aac']})")
    lines += ["", "All individual WAVs and decoded A/B AAC files passed finite/non-clipping checks. "
              "The JSON records input/source/media SHA-256 digests and per-probe renderer/export metrics.", ""]
    (demos.PROJECT_DIR / "Docs/research/touch-comparison.md").write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-baseline", action="store_true")
    args = parser.parse_args()
    baseline_manifest = BASELINE_DIR / "baseline_probes.json"
    if args.capture_baseline or not baseline_manifest.exists():
        before = capture_baseline()
    else:
        before = json.loads(baseline_manifest.read_text())
    if args.capture_baseline:
        print(f"Captured {len(before['probes'])} baseline probes from {BASELINE_REVISION}")
        return
    if demos.sha256(BASELINE_DIR / "baseline_renderer") != before["renderer_sha256"]:
        raise RuntimeError("Baseline renderer binary changed")
    snapshot = json.loads((BASELINE_DIR / "source_manifest.json").read_text())
    if before["revision"] != BASELINE_REVISION or snapshot["revision"] != BASELINE_REVISION:
        raise RuntimeError("Unexpected baseline revision")
    for name, digest in snapshot["source_sha256"].items():
        if demos.sha256(BASELINE_DIR / "source" / name) != digest:
            raise RuntimeError("Baseline source snapshot changed: " + name)
    for row in before["probes"]:
        if demos.sha256(BASELINE_DIR / (row["name"] + ".wav")) != row["wav_sha256"]:
            raise RuntimeError("Baseline audio changed: " + row["name"])
    # Older protected captures contain the original 40 C4 probes. Extend only
    # missing declared probes with that same verified pinned renderer; retain
    # and re-check every existing WAV instead of replacing the baseline.
    declared = list(probe_specs())
    old_by_name = {row["name"]: row for row in before["probes"]}
    if set(old_by_name) - {row["name"] for row in declared}:
        raise RuntimeError("Baseline contains probes outside the declared specification")
    for spec in declared:
        if spec["name"] in old_by_name and any(old_by_name[spec["name"]][key] != spec[key]
                                               for key in ["model", "mallet", "position", "velocity", "note"]):
            raise RuntimeError("Baseline probe specification changed: " + spec["name"])
    missing = [spec for spec in declared if spec["name"] not in old_by_name]
    if missing:
        added = render_probes(BASELINE_DIR / "baseline_renderer", BASELINE_DIR, missing)
        old_by_name.update({row["name"]: row for row in added})
        before["probes"] = [old_by_name[spec["name"]] for spec in declared]
        baseline_manifest.write_text(json.dumps(before, indent=2) + "\n")
        print(f"Added {len(added)} missing baseline probes with the verified pinned renderer")
    initial_sources = source_hashes(demos.PROJECT_DIR)
    demos.compile_renderer()
    after = render_probes(demos.RENDERER_BIN, CURRENT_DIR)
    if source_hashes(demos.PROJECT_DIR) != initial_sources:
        raise RuntimeError("Source changed during touch comparison; rerun after DSP stabilizes")
    rows = []
    old_by_name = {row["name"]: row for row in before["probes"]}
    for current in after:
        old = old_by_name[current["name"]]
        # Neither side may silently change the experimental settings.
        for field in ["model", "mallet", "position", "velocity", "note"]:
            if current[field] != old[field]:
                raise RuntimeError("Mismatched probe setting: " + current["name"])
        rows.append({**{k: current[k] for k in ["name", "model", "mallet", "position", "velocity", "note"]},
                     "before": main_bus_metrics(BASELINE_DIR / (current["name"] + ".wav")),
                     "after": main_bus_metrics(CURRENT_DIR / (current["name"] + ".wav")),
                     "before_renderer": {k: v for k, v in old["metrics"].items() if k != "output_file"},
                     "after_renderer": {k: v for k, v in current["metrics"].items() if k != "output_file"},
                     "before_wav_sha256": old["wav_sha256"], "after_wav_sha256": current["wav_sha256"]})
    ab = [ab_audition(f"{NAMES[model]}_dynamics", [f"m{model}_velocity{v}" for v in VELOCITIES]) for model in range(4)]
    ab += [ab_audition("Kalimba_plucks", [f"m3_pluck{mallet}" for mallet in range(4)])]
    ab += [ab_audition(f"{NAMES[model]}_register", [f"m{model}_velocity16", f"m{model}_velocity127",
                                                 f"m{model}_C6_velocity16", f"m{model}_C6_velocity127"]) for model in range(4)]
    result = {"schema_version": 1, "baseline_revision": BASELINE_REVISION,
              "baseline_source_sha256": snapshot["source_sha256"], "current_source_sha256": initial_sources,
              "comparison_script_sha256": demos.sha256(Path(__file__)),
              "baseline_renderer_sha256": before["renderer_sha256"],
              "current_renderer_sha256": demos.sha256(demos.RENDERER_BIN), "probes": rows, "ab_auditions": ab}
    (demos.PROJECT_DIR / "Docs/research/touch-comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    write_report(result)
    print(f"Compared {len(rows)} controlled probes and verified {len(ab)} fixed-gain A/B auditions")


if __name__ == "__main__":
    main()
