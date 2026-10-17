#!/usr/bin/env python3
"""Render an equal-gain Strike Variance comparison against a frozen DSP tree.

Requires numpy and clang++. The baseline must contain DSP/ and the matching
Tools/Demos/Showcase/render_showcase.cpp. Outputs are evidence, not release demos.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import subprocess
import wave

import numpy as np

PROJECT = Path(__file__).resolve().parents[2]
def dsp_dir(root):
    """A pre-move baseline root keeps DSP/ at its top; the current tree has the submodule."""
    return root / "DSP" if (root / "DSP").is_dir() else root / "Source/DSP"

RATE = 48000
AMOUNTS = (0, 5, 15, 25, 50, 100)
MODELS = ("Rosewood", "Padauk", "Balafon", "Kalimba")
ONSETS = [round((.1 + .65 * hit) * RATE) for hit in range(8)]
DURATION = 6


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / "comparison-report.json"
    if report_path.exists():
        raise SystemExit("Refusing to overwrite an existing comparison report")
    roots = {"before": args.baseline_root.resolve(), "after": PROJECT}
    source_hashes = {}
    for version, root in roots.items():
        renderer = root / "Tools/Demos/Showcase/render_showcase.cpp"
        sources = sorted(dsp_dir(root).glob("*.cpp"))
        subprocess.run(["clang++", "-std=c++17", "-O3", "-Wall", "-Wextra", "-Werror",
                        "-I", str(dsp_dir(root).parent), str(renderer), *map(str, sources),
                        "-o", str(out / version)], check=True)
        source_hashes[version] = {str(path.relative_to(root)): digest(path)
                                 for path in sorted(dsp_dir(root).glob("*")) if path.is_file()}
        source_hashes[version][str(renderer.relative_to(root))] = digest(renderer)

    events = out / "repeated-c4.events"
    events.write_text("".join(f"{frame / RATE:.9f} 1 60 90\n"
                              f"{frame / RATE + .12:.9f} 0 60 0\n" for frame in ONSETS))
    base = dict(line.split() for line in
                (PROJECT / "Tools/Demos/Showcase/01_Rosewood_Afterglow.parameters.txt").read_text().splitlines())
    base.update(malletType=1, malletHardness=.5, strikePosition=.45, decay=.4,
                buzzAmount=0, artifacts=0, sympathetic=0, bodyBloom=0, pitchGlide=0,
                farLevel=0, piezoLevel=0, closeLevel=1, stereoWidth=1,
                preampDrive=0, warmth=0, compAmount=0, volume=.8,
                oversampling=0, polyphony=1, velocityCurve=0, tuneCents=0, detune=0,
                velocityToHardness=0, velocityToPosition=0, velocityToDecay=0)
    rows, peak = [], 0.0
    for model, name in enumerate(MODELS):
        for amount in AMOUNTS:
            settings = out / f"{name}-{amount}.parameters.txt"
            params = dict(base, model=model, strikeJitter=amount)
            settings.write_text("".join(f"{key} {value}\n" for key, value in params.items()))
            row = dict(model=name, variance=amount)
            for version in roots:
                raw = out / f"{name}-{amount}-{version}.f32"
                result = subprocess.run([str(out / version), str(settings), str(events),
                                         str(raw), str(DURATION), str(RATE)],
                                        check=True, capture_output=True, text=True)
                data = np.fromfile(raw, dtype="<f4").reshape(-1, 2).astype(np.float64)
                assert data.shape == (RATE * DURATION, 2) and np.isfinite(data).all()
                levels = [10 * np.log10(np.mean(data[start:start + round(.04 * RATE)] ** 2))
                          for start in ONSETS]
                peak = max(peak, float(np.max(np.abs(data))))
                row[version] = dict(json.loads(result.stdout), raw_file=raw.name,
                                    raw_sha256=digest(raw), attack_levels_db=levels,
                                    attack_span_db=max(levels) - min(levels))
            row["bit_identical"] = row["before"]["raw_sha256"] == row["after"]["raw_sha256"]
            if amount in (0, 100):
                assert row["bit_identical"], f"Endpoint changed: {name} {amount}"
            rows.append(row)

    # One gain for every instrument, amount, hit and version; no compression.
    gain = min(1.0, .9 / peak)
    for row in rows:
        for version in roots:
            raw = out / row[version]["raw_file"]
            data = np.fromfile(raw, dtype="<f4")
            pcm = np.rint(data.astype(np.float64) * gain * 32767).astype("<i2")
            wav = raw.with_suffix(".wav")
            with wave.open(str(wav), "wb") as writer:
                writer.setnchannels(2)
                writer.setsampwidth(2)
                writer.setframerate(RATE)
                writer.writeframes(pcm.tobytes())
            row[version]["wav_file"] = wav.name
            row[version]["wav_sha256"] = digest(wav)
    report = dict(sample_rate=RATE, oversampling=2, midi_note=60, midi_velocity=90,
                  note_on_frames=ONSETS, duration_seconds=DURATION, common_wav_gain=gain,
                  source_sha256=source_hashes, cases=rows,
                  limits="Phrase attack spans include ringing tails; isolated pitched-timbre checks are in StrikeVarianceTest.")
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    sections = []
    for name in MODELS:
        entries = []
        for row in rows:
            if row["model"] != name:
                continue
            cells = [f'<th scope="row">{row["variance"]}%</th>']
            for version in roots:
                item = row[version]
                cells.append(f'<td><audio controls preload="none" src="{html.escape(item["wav_file"])}"></audio>'
                             f'<small>Attack span: {item["attack_span_db"]:.2f} dB</small></td>')
            entries.append("<tr>" + "".join(cells) + "</tr>")
        sections.append(f"<h2>{name}</h2><div class=scroll><table><thead><tr><th>Variance</th>"
                        "<th>Previous curve</th><th>Smooth curve</th></tr></thead><tbody>"
                        + "".join(entries) + "</tbody></table></div>")
    (out / "variance-review.html").write_text("""<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Maremba · Strike Variance</title>
<style>body{max-width:900px;margin:40px auto;padding:0 20px;background:#171b1b;color:#ede9df;font:16px/1.6 system-ui}
h1,h2{font-weight:500;color:#e9bf79}p{max-width:760px}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:12px;border-bottom:1px solid #3b4241}
small{display:block;color:#aaa}audio{max-width:100%;width:290px}.scroll{overflow-x:auto}a{color:#e9bf79}</style>
<h1>Maremba · Strike Variance</h1><p>Eight identical C4 hits at MIDI velocity 90. Compare 5% and 15% first:
the smooth curve responds from the start of the knob. Zero and maximum are unchanged.</p>
<p>48 kHz, default 2× quality. Room, Artifacts, Buzz, Bloom, Sympathy, Drive and compression are off.
All clips share one gain; no per-hit or per-clip normalization. Attack spans measure this phrase,
including ringing tails, and are not a perceptual score.</p>""" + "".join(sections) + """
<p><a href="comparison-report.json">Measurement report and source hashes</a></p>
<script>document.addEventListener('play',event=>{if(event.target.tagName==='AUDIO')document.querySelectorAll('audio').forEach(a=>{if(a!==event.target)a.pause()})},true)</script></html>""")
    print(json.dumps(dict(report=str(report_path), player=str(out / "variance-review.html"),
                          cases=len(rows), endpoint_pairs_bit_identical=8), indent=2))


if __name__ == "__main__":
    main()
