#!/usr/bin/env python3
"""Freeze, compile and compare before/after Maremba physical-model DSP.

Requires Python 3, numpy and clang++. Run after DSP editing has finished:
  python3 Tools/Demos/build_physics_review.py

The initial baseline is /tmp/maremba-realism-baseline/DSP. Later runs preserve
the review's sources/before/DSP unless --baseline explicitly chooses another.
Every WAV uses one common gain; optional browser RMS matching attenuates playback
only. No numerical metric or generated clip implies a completed listening test.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import wave

import numpy as np


PROJECT = Path(__file__).resolve().parents[2]
MODELS = ("Imperial Rosewood", "Mayan Padauk", "Balafon Ancestral", "Kalimba Artisan")
SETUPS = {"dry_close": "Dry Close", "default_mix": "Default mix", "hard_far": "Hard contact · dry Far", "quiet_restrike": "Quiet restrike · dry Close"}
SETUP_DESCRIPTIONS = {
    "quiet_restrike": "Soft striker 0, Hardness 40%, Position 35%, Decay 4, Coupling 70%; body, support, room, effects, artifacts and velocity modulation off. Six loud C3/C4 strikes followed 40–47 ms later by quiet restrikes, then the standard final chord. Exposes reaction to an already moving bar.",
    "dry_close": "Close microphone isolated in the main mix; Coupling 70%, Buzz 15%; body, sympathetic support, room and output effects off.",
    "default_mix": "Default body, sympathetic support, microphones, room, preamp and limiter settings.",
    "hard_far": "Hard striker 3, Hardness 90%, Position 35%, Coupling 100%; direct Far taps with room, body, sympathetic support, buzz and artifacts off. Exposes upper-mode radiation under hard off-centre contact; not a claim that default soft strokes change dramatically. The direct tap retains its fixed -6 dB trim and master volume.",
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(directory):
    return {p.name: sha256(p) for p in sorted(directory.iterdir())
            if p.is_file() and p.suffix in (".cpp", ".h")}


def db(value):
    return round(float(20 * np.log10(max(float(value), 1e-15))), 3)


def rms(samples):
    return float(np.sqrt(np.mean(samples.astype(np.float64) ** 2)))


def metrics(samples, rate):
    mono = samples.mean(axis=1)
    window_size, hop = 2048, 512
    windows = np.lib.stride_tricks.sliding_window_view(mono, window_size)[::hop]
    spectrum = np.abs(np.fft.rfft(windows * np.hanning(window_size), axis=1))
    frequencies = np.fft.rfftfreq(window_size, 1 / rate)
    centroid = float(np.sum(spectrum * frequencies) / max(np.sum(spectrum), 1e-30))
    tail = samples[int(10.2 * rate):]
    chord_attack = samples[int(8.2 * rate):int(8.4 * rate)]
    level = rms(samples)
    peak = float(np.max(np.abs(samples)))
    return {"peak_dbfs": db(peak), "rms_dbfs": db(level), "rms_linear": level,
            "crest_db": db(peak / max(level, 1e-15)),
            "magnitude_weighted_centroid_hz": round(centroid, 3),
            "late_tail_rms_dbfs": db(rms(tail)),
            "late_tail_relative_to_chord_attack_db": db(rms(tail) / max(rms(chord_attack), 1e-15))}


def write_wav(path, samples, rate, gain):
    scaled = samples.astype(np.float64) * gain
    if not np.isfinite(scaled).all() or np.max(np.abs(scaled)) >= 1.0:
        raise RuntimeError(f"Non-finite or clipped output at common gain {gain}: {path}")
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(3)
        output.setframerate(rate)
        pcm = np.rint(scaled * 8388607).astype("<i4").reshape(-1)
        packed = np.empty((len(pcm), 3), dtype=np.uint8)
        for byte in range(3):
            packed[:, byte] = (pcm >> (8 * byte)) & 255
        output.writeframes(packed.tobytes())
    return metrics(scaled, rate)


def build_stage(stage, source, output, renderer):
    hashes = source_hashes(source)
    if "MarembaEngine.cpp" not in hashes:
        raise RuntimeError(f"No DSP engine at {source}")
    frozen = output / "sources" / stage / "DSP"
    frozen.mkdir(parents=True, exist_ok=True)
    if source.resolve() != frozen.resolve():
        for old in frozen.iterdir():
            if old.is_file() and old.suffix in (".h", ".cpp") and old.name not in hashes:
                old.unlink()
        for name in hashes:
            shutil.copy2(source / name, frozen / name)
    if hashes != source_hashes(source) or hashes != source_hashes(frozen):
        raise RuntimeError(f"DSP changed while freezing {stage}; rerun after editing finishes")
    binary = output / f"renderer-{stage}"
    command = ["clang++", "-std=c++17", "-O3", "-Wall", "-Wextra", "-Werror",
               "-I", str(frozen), str(renderer),
               *[str(frozen / name) for name in hashes if name.endswith(".cpp")],
               "-o", str(binary)]
    build = subprocess.run(command, capture_output=True, text=True)
    (output / f"build-{stage}.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    raw = output / "raw" / stage
    raw.mkdir(parents=True, exist_ok=True)
    render = subprocess.run([str(binary), str(raw)], capture_output=True, text=True)
    (output / f"render-{stage}.log").write_text(render.stdout + render.stderr)
    render.check_returncode()
    return hashes, json.loads((raw / "render-manifest.json").read_text())


def write_page(output, report):
    cards = []
    for model, name in enumerate(MODELS):
        rows = []
        for setup, label in SETUPS.items():
            choices = "".join(
                f'<button type="button" data-clip="{stage}_model_{model}_{setup}" aria-pressed="false">'
                f'{stage.title()}</button>' for stage in ("before", "after"))
            rows.append(f'<p>{label}</p><div class="choices">{choices}</div>')
        cards.append(f'<section><h2>{html.escape(name)}</h2>{"".join(rows)}</section>')
    page = '''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Maremba physical model A/B</title>
<style>:root{font:16px/1.5 system-ui;color:#f0ede6;background:#19211e;color-scheme:dark}*{box-sizing:border-box}
body{max-width:1000px;margin:auto;padding:30px 22px 65px}h1{line-height:1.2}p{color:#c0c9c3}
main{display:grid;grid-template-columns:1fr 1fr;gap:16px}section{padding:21px;border:1px solid #4d6154;border-radius:10px}
h2{font-size:21px;margin:0}section p{font-size:14px;margin:19px 0 7px}.choices{display:flex;gap:10px}
button{min-width:95px;padding:10px;border:1px solid #82927c;border-radius:5px;background:#304135;color:inherit;font:inherit;cursor:pointer}
button[aria-pressed=true]{background:#e4bd79;color:#17251c}button:focus-visible{outline:3px solid #99d1f0;outline-offset:2px}
.player{position:sticky;top:0;padding:15px 0;background:#19211ef5;margin:22px 0;z-index:2}audio{width:100%;margin:10px 0}
label{display:inline-block;margin:0 15px 7px 0;font-size:14px}a{color:#e4bd79;font-size:14px}#status{font-weight:600}
#measurement{font-size:13px;min-height:21px;color:#c0c9c3}details{margin-top:22px}li{margin:8px 0}
@media(max-width:660px){main{grid-template-columns:1fr}.player{position:static}}</style></head><body>
<h1>Maremba · physical model A/B</h1>
<p>Compare the saved engine before this change with the revised engine. The same 14-second score plays soft-to-hard strikes, repeated notes, low/high notes and a short chord, followed by the natural tail.</p>
<p><strong>Dry Close</strong> isolates the close microphone with Coupling 70% and Buzz 15%; body, sympathetic support, room and output effects are off. <strong>Default mix</strong> includes the default body, sympathetic support, microphones, room, preamp and limiter.</p>
<p><strong>Hard contact · dry Far</strong> uses striker 3, Hardness 90%, Position 35% and Coupling 100%. It records the dry Far direct output with body, sympathetic support, buzz and artifacts off to expose upper-mode radiation. This is a focused comparison; it does not imply large changes to default soft strokes.</p>
<p><strong>Quiet restrike · dry Close</strong> uses soft striker 0, Hardness 40%, Position 35%, Decay 4 and Coupling 70%. Six loud C3/C4 strikes receive a quiet restrike 40–47 ms later, with body, support, room, effects and playing artifacts off.</p>
<div class="player"><div id="status" aria-live="polite">Choose a comparison to play</div>
<audio id="player" controls preload="metadata"></audio><div id="measurement"></div>
<label><input id="keep-time" type="checkbox" checked> Keep position when switching</label>
<label><input id="match" type="checkbox"> RMS-match this A/B pair (playback only)</label>
<a id="download" hidden download>Download selected WAV</a></div><main>'''
    page += "".join(cards)
    page += (f'</main><p>One encoded playback gain for all files: {report["common_playback_gain"]:g}× '
             f'({report["common_playback_gain_db"]:+.2f} dB). Files are never normalized individually. '
             'WAVs are stereo 24-bit PCM. RMS matching only turns down the louder version of the selected model/setup; it does not change the WAVs.</p>'
             '<details><summary>Score and measurement limits</summary><ul>'
             '<li>The standard score is used for the first three setups. Quiet restrike uses a separate score recorded in the manifest; the final chord and tail windows are shared.</li>'
             '<li>0.15–2.70 s: C4 at velocities 25%, 45%, 70%, 100%.</li>'
             '<li>3.70–4.75 s: four G4 strikes at 75%. 5.80–7.00 s: C3, G3, C5.</li>'
             '<li>8.20 s: C4/E4/G4 chord at 72%; keys release after 120 ms, while the instrument rings freely.</li>'
             '<li>Metrics use the float render after the common gain, before PCM encoding. Tail measurement covers 10.20–14.00 s; silence is reported at a −300 dB floor. Centroid uses mono magnitude spectra from 2048-sample Hann windows, hop 512.</li>'
             '<li>Hard contact uses the direct Far taps, with their fixed calibration and no master volume; the room and main microphone faders are zero. The other setups use the main output. Compare Before/After within each setup.</li>'
             '<li>Numerical differences show changed behavior; they do not establish realism or pleasantness. These are native engine renders, not Reason host recordings. No listening assessment is claimed.</li>'
             '</ul></details><p><a href="physics-review.json">Measurements, exact score and source hashes</a></p><script>const clips=')
    page += json.dumps(report["clips"])
    page += ''';const player=document.querySelector('#player'),status=document.querySelector('#status');
let pending=0,selected=null;function setLevel(){if(!selected)return;const c=clips[selected];
const pair=Object.values(clips).filter(x=>x.model===c.model&&x.setup===c.setup);
const quieter=Math.min(...pair.map(x=>x.rms_linear));const match=document.querySelector('#match').checked;
player.volume=match?Math.min(1,quieter/Math.max(c.rms_linear,1e-15)):1;
document.querySelector('#measurement').textContent='Peak '+c.peak_dbfs.toFixed(1)+' dBFS · RMS '+c.rms_dbfs.toFixed(1)
+' dBFS · centroid '+Math.round(c.magnitude_weighted_centroid_hz)+' Hz · playback '+(20*Math.log10(player.volume)).toFixed(1)+' dB';}
document.querySelector('#match').onchange=setLevel;
document.querySelectorAll('[data-clip]').forEach(button=>button.onclick=()=>{selected=button.dataset.clip;
const c=clips[selected],time=document.querySelector('#keep-time').checked?player.currentTime:0,request=++pending;
player.pause();player.src=c.file;setLevel();document.querySelectorAll('[data-clip]').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));
status.textContent=c.model+' · '+c.setup_label+' · '+c.stage;const link=document.querySelector('#download');link.href=c.file;link.hidden=false;
player.onloadedmetadata=()=>{if(request!==pending)return;player.currentTime=time<player.duration?time:0;
player.play().catch(()=>{status.textContent+=' · press Play to start';});};player.load();});
</script></body></html>'''
    (output / "physics-review.html").write_text(page)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=PROJECT / "Release/validation/physical-models-20260922")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--gain", type=float, default=1.0)
    args = parser.parse_args()
    if not np.isfinite(args.gain) or args.gain <= 0:
        parser.error("--gain must be finite and positive")
    output = args.output.resolve()
    persistent = output / "sources/before/DSP"
    baseline = args.baseline or (persistent if persistent.is_dir() else Path("/tmp/maremba-realism-baseline/DSP"))
    if not baseline.is_dir():
        parser.error("--baseline must name the frozen pre-change DSP directory")
    output.mkdir(parents=True, exist_ok=True)
    audio_dir = output / "audio"
    audio_dir.mkdir(exist_ok=True)
    renderer = PROJECT / "Tools/Demos/render_physics_review.cpp"
    frozen_renderer = output / "sources" / renderer.name
    frozen_renderer.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(renderer, frozen_renderer)
    shutil.copy2(Path(__file__), output / "sources" / Path(__file__).name)
    report = {
        "scope": "Native DSP A/B review; no Reason host recording or completed listening assessment",
        "common_playback_gain": args.gain, "common_playback_gain_db": db(args.gain),
        "normalization": "No per-file normalization; optional browser-only attenuation to pair minimum RMS",
        "renderer_sha256": sha256(frozen_renderer), "builder_sha256": sha256(Path(__file__)),
        "toolchain": {"compiler": subprocess.run(["clang++", "--version"], capture_output=True, text=True, check=True).stdout.strip(),
                      "python": sys.version, "numpy": np.__version__, "platform": platform.platform()},
        "wav_format": "Stereo PCM24 little-endian",
        "metrics": {"domain": "Float render after common gain, before PCM encoding; dB floor -300",
                    "centroid": "Mono magnitude-weighted STFT, Hann 2048, hop 512",
                    "tail_seconds": [10.2, 14.0], "chord_attack_seconds": [8.2, 8.4]},
        "source_sha256": {}, "clips": {}, "comparisons": {},
        "setup_descriptions": SETUP_DESCRIPTIONS,
    }
    rendered, score = {}, None
    for stage, source in (("before", baseline.resolve()), ("after", PROJECT / "Source/DSP")):
        print(f"Building and rendering {stage} from {source}", flush=True)
        hashes, manifest = build_stage(stage, source, output, frozen_renderer)
        report["source_sha256"][stage] = hashes
        if score is not None and manifest != score:
            raise RuntimeError("Before/after renderer settings differ")
        score = manifest
        for model, name in enumerate(MODELS):
            for setup, label in SETUPS.items():
                take = f"model_{model}_{setup}"
                raw_file = output / "raw" / stage / f"{take}.f32"
                samples = np.fromfile(raw_file, dtype="<f4").reshape(-1, 2)
                if len(samples) != manifest["frames"] or not np.isfinite(samples).all():
                    raise RuntimeError(f"Invalid raw samples: {raw_file}")
                rendered[(stage, model, setup)] = samples
                key = f"{stage}_{take}"
                wav = audio_dir / f"{key}.wav"
                measured = write_wav(wav, samples, manifest["sample_rate"], args.gain)
                report["clips"][key] = {"file": str(wav.relative_to(output)), "model": name,
                    "setup": setup, "setup_label": label, "stage": stage.title(),
                    "seconds": len(samples) / manifest["sample_rate"], **measured,
                    "wav_sha256": sha256(wav), "raw_sha256": sha256(raw_file)}
    if source_hashes(PROJECT / "Source/DSP") != report["source_sha256"]["after"]:
        raise RuntimeError("Current DSP changed after freezing; rerun to review the final workspace version")
    report.update(score)
    for model, name in enumerate(MODELS):
        report["comparisons"][name] = {}
        for setup in SETUPS:
            before = rendered[("before", model, setup)].astype(np.float64)
            after = rendered[("after", model, setup)].astype(np.float64)
            report["comparisons"][name][setup] = {
                "after_minus_before_rms_db_relative_to_before": db(rms(after - before) / max(rms(before), 1e-15)),
                "after_vs_before_rms_db": db(rms(after) / max(rms(before), 1e-15)),
            }
    (output / "physics-review.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    write_page(output, report)
    print(output / "physics-review.html")
    print(json.dumps({"clips": len(report["clips"]), "common_gain": args.gain,
                      "comparisons": report["comparisons"]}, indent=2))


if __name__ == "__main__":
    main()
