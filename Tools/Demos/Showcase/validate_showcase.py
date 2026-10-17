#!/usr/bin/env python3
"""Independent delivery checks for the four original Maremba showcases."""
import argparse
import base64
import hashlib
import importlib.util
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--input", type=Path, default=HERE,
                           help="Generated showcase directory; source checks use this checkout")
    args = arguments.parse_args()
    output = args.input.expanduser().resolve()
    manifest = json.loads((output / "SHOWCASE_MANIFEST.json").read_text())
    spec = importlib.util.spec_from_file_location("existing_demo_parser", HERE.parent / "run_benchmark.py")
    parser = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parser)
    version = re.search(r"project\(Maremba VERSION ([0-9.]+)", (PROJECT / "CMakeLists.txt").read_text()).group(1)
    assert manifest["engine_version"] == version
    assert manifest["adapter_sha256"] == sha(HERE / "render_showcase.cpp")
    assert manifest["composer_sha256"] == sha(HERE / "build_showcase.py")
    assert manifest["format_version"] == 2
    assert manifest["compile_returncode"] == 0
    for name, digest in manifest["dsp_files"].items():
        assert sha(PROJECT / name) == digest, f"Changed source: {name}"
    digest = hashlib.sha256(json.dumps(manifest["dsp_files"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    assert digest == manifest["dsp_source_sha256"]
    header = (PROJECT / "Source/DSP/MarembaEngine.h").read_text().split("struct EngineParameters {")[1].split("};")[0]
    fields = set(re.findall(r"(?:int|float)\s+(\w+)\s*=", header))
    html = (output / "Maremba_Showcase.html").read_text()
    embedded = json.loads(html.split("const tracks=", 1)[1].split(";\nconst instruments=", 1)[0])
    assert len(embedded) == len(manifest["tracks"]) == 4
    assert {t["model"] for t in manifest["tracks"]} == {0, 1, 2, 3}
    results = []
    for track, preview in zip(manifest["tracks"], embedded):
        slug = track["slug"]
        assert 20 <= track["duration_seconds"] <= 35
        assert set(track["parameters"]) == fields
        assert track["parameters"]["preampDrive"] == .01
        assert track["parameters"]["buzzAmount"] == track["parameters"]["compAmount"] == 0
        assert track["constant_gain_db"] == manifest["common_gain_db"]
        for filename, record in track["files"].items():
            file = output / filename
            assert file.stat().st_size == record["bytes"]
            assert sha(file) == record["sha256"], f"Changed output: {filename}"
        midi_events = parser.parse_smf((output / f"{slug}.mid").read_bytes())
        rows = [line.split() for line in (output / f"{slug}.events").read_text().splitlines() if line and not line.startswith("#")]
        events = [(float(t), int(kind), int(note), int(velocity)) for t, kind, note, velocity in rows]
        assert len(events) == len(midi_events) == track["note_count"] * 2
        assert all(a[1:] == b[1:] and abs(a[0]-b[0]) <= 5.1e-10 for a, b in zip(events, midi_events))
        assert base64.b64decode(preview["audio"].split(",", 1)[1]) == (output / f"{slug}.m4a").read_bytes()
        assert preview["title"] == track["title"]
        assert track["raw_engine"]["nonfinite_samples"] == 0
        for codec in ("wav", "aac"):
            assert track[f"{codec}_metrics"]["nonfinite_samples"] == 0
            assert track[f"{codec}_metrics"]["clipped_samples"] == 0
            assert track[f"{codec}_loudness"]["input_tp"] < -.5
        assert track["wav_metrics"]["end_rms_dbfs"] < -60
        assert track["duration_seconds"] - track["final_sample_above_minus90_seconds"] >= .599
        results.append(f"PASS {slug}: {track['note_count']} exact MIDI/events, explicit parameters, hashed WAV/AAC, embedded AAC bytes and natural tail")
    javascript = re.search(r"<script>([\s\S]*?)</script>", html).group(1)
    with tempfile.TemporaryDirectory(prefix="maremba-showcase-js-") as temporary:
        path = Path(temporary) / "player.js"
        path.write_text(javascript)
        subprocess.run(["node", "--check", path], check=True, capture_output=True)
    results.extend(["PASS HTML player JavaScript syntax (node --check)",
                    "PASS current device version, full DSP source set, renderer and composer hashes",
                    "PASS Buzz 0%, Drive 1%, limiter disabled and one common gain across all four tracks",
                    "SCOPE: browser layout/playback checks are recorded separately in Maremba_Player_Validation.json when performed.",
                    "LIMIT: no model-accessible listening tool; no subjective audition claimed."])
    text = "\n".join(results) + "\n"
    (output / "VALIDATION.txt").write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
