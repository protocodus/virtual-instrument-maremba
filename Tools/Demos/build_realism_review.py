#!/usr/bin/env python3
"""Build reproducible direct-output WAVs and a local listening review.

Requires clang++, Python 3 and numpy. Run from any directory:
  python3 Tools/Demos/build_realism_review.py
  python3 Tools/Demos/build_realism_review.py --baseline /path/to/before/DSP --gain 1

Playback gain is a fixed, shared multiplier, never a per-file normalization.
Any non-finite sample or clipped output fails the build. Frozen source copies
and SHA-256 manifests identify exactly what the two native renderers used.
The baseline defaults to the output's persistent sources/before/DSP snapshot;
/tmp/maremba-realism-baseline/DSP is used only to create the first review.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess
import wave

import numpy as np


PROJECT = Path(__file__).resolve().parents[2]
MODELS = ("Imperial Rosewood", "Mayan Padauk", "Balafon Ancestral", "Kalimba Artisan")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(directory):
    return {p.name: sha256(p) for p in sorted(directory.iterdir())
            if p.suffix in (".cpp", ".h")}


def db(value):
    return round(float(20 * np.log10(max(float(value), 1e-15))), 3)


def metrics(samples):
    peak = float(np.max(np.abs(samples)))
    rms = float(np.sqrt(np.mean(samples.astype(np.float64) ** 2)))
    return {"peak_dbfs": db(peak), "rms_dbfs": db(rms), "crest_db": db(peak / max(rms, 1e-15))}


def write_wav(path, samples, rate, gain):
    scaled = samples.astype(np.float64) * gain
    if not np.isfinite(scaled).all() or np.max(np.abs(scaled)) >= 1.0:
        raise RuntimeError(f"Non-finite or clipped output at common gain {gain}: {path}")
    with wave.open(str(path), "wb") as output:
        output.setnchannels(samples.shape[1])
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(np.rint(scaled * 32767).astype("<i2").tobytes())
    return metrics(scaled)


def build_stage(stage, source, output, renderer):
    before = source_hashes(source)
    if not before or "MarembaEngine.cpp" not in before:
        raise RuntimeError(f"No DSP engine at {source}")
    frozen = output / "sources" / stage / "DSP"
    frozen.mkdir(parents=True, exist_ok=True)
    if source.resolve() != frozen.resolve():
        for name in before:
            shutil.copy2(source / name, frozen / name)
    if before != source_hashes(source) or before != source_hashes(frozen):
        raise RuntimeError(f"DSP source changed while freezing {stage}; rerun after editing finishes")
    raw = output / "raw" / stage
    raw.mkdir(parents=True, exist_ok=True)
    binary = output / f"renderer-{stage}"
    command = ["clang++", "-std=c++17", "-O3", "-Wall", "-Wextra", "-Werror",
               "-I", str(frozen), str(renderer),
               *[str(frozen / name) for name in before if name.endswith(".cpp")],
               "-o", str(binary)]
    build = subprocess.run(command, capture_output=True, text=True)
    (output / f"build-{stage}.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    render = subprocess.run([str(binary), str(raw)], capture_output=True, text=True)
    (output / f"render-{stage}.log").write_text(render.stdout + render.stderr)
    render.check_returncode()
    return before, json.loads((raw / "render-manifest.json").read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=PROJECT / "Release/validation/engine-realism-20260922")
    parser.add_argument("--baseline", type=Path,
                        help="Frozen pre-change DSP directory; defaults to the saved review snapshot")
    parser.add_argument("--gain", type=float, default=1.0)
    args = parser.parse_args()
    if not np.isfinite(args.gain) or args.gain <= 0:
        parser.error("--gain must be finite and positive")
    output = args.output.resolve()
    baseline = args.baseline
    if baseline is None:
        persistent = output / "sources" / "before" / "DSP"
        baseline = persistent if persistent.is_dir() else Path("/tmp/maremba-realism-baseline/DSP")
    if not baseline.is_dir():
        parser.error("--baseline must name the frozen pre-change DSP directory")
    output.mkdir(parents=True, exist_ok=True)
    audio_dir = output / "audio"
    audio_dir.mkdir(exist_ok=True)
    renderer = PROJECT / "Tools/Demos/render_realism_review.cpp"
    frozen_renderer = output / "sources" / renderer.name
    frozen_renderer.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(renderer, frozen_renderer)
    shutil.copy2(Path(__file__), output / "sources" / Path(__file__).name)
    report = {
        "scope": "Native engine direct-output listening review; no Reason host validation",
        "common_playback_gain": args.gain,
        "common_playback_gain_db": db(args.gain),
        "normalization": "None; one fixed multiplier for every model, version and output",
        "renderer_sha256": sha256(frozen_renderer),
        "source_sha256": {}, "clips": {}, "comparisons": {},
    }
    rendered = {}
    score = None
    for stage, source in (("before", baseline.resolve()), ("after", PROJECT / "Source/DSP")):
        print(f"Building and rendering {stage} from {source}", flush=True)
        hashes, manifest = build_stage(stage, source, output, frozen_renderer)
        report["source_sha256"][stage] = hashes
        if score is not None and manifest != score:
            raise RuntimeError("Before/after renderer settings differ")
        score = manifest
        for model in range(4):
            for amount in (0, 35, 50, 100):
                take = f"model_{model}_artifacts_{amount}"
                source_file = output / "raw" / stage / f"{take}.f32"
                samples = np.fromfile(source_file, dtype="<f4").reshape(-1, 3)
                if len(samples) != manifest["frames"] or not np.isfinite(samples).all():
                    raise RuntimeError(f"Invalid sample data in {source_file}")
                rendered[(stage, model, amount)] = samples
                routes = {"close": samples[:, :2]}
                if amount == 35:
                    routes["piezo"] = samples[:, 2:3]
                for route, routed in routes.items():
                    key = f"{stage}_{take}_{route}"
                    path = audio_dir / f"{key}.wav"
                    measured = write_wav(path, routed, manifest["sample_rate"], args.gain)
                    report["clips"][key] = {
                        "file": str(path.relative_to(output)), "model": MODELS[model],
                        "stage": stage, "route": route, "artifacts_percent": amount,
                        "channels": routed.shape[1], "seconds": len(routed) / manifest["sample_rate"],
                        **measured, "wav_sha256": sha256(path), "raw_sha256": sha256(source_file),
                    }

    report.update(score)
    for model in range(4):
        comparison = {}
        for stage in ("before", "after"):
            low = rendered[(stage, model, 0)][:, :2].astype(np.float64)
            high = rendered[(stage, model, 100)][:, :2].astype(np.float64)
            capture = rendered[(stage, model, 35)].astype(np.float64)
            close = capture[:, :2].mean(axis=1)
            piezo = capture[:, 2]
            correlation = np.corrcoef(close, piezo)[0, 1]
            comparison[stage] = {
                "artifacts_100_minus_0_db_relative_to_0": db(
                    np.sqrt(np.mean((high - low) ** 2) / max(np.mean(low ** 2), 1e-30))),
                "close_piezo_35_sample_correlation": round(float(correlation), 6),
                "piezo_vs_close_mono_35_rms_db": db(
                    np.sqrt(np.mean(piezo ** 2) / max(np.mean(close ** 2), 1e-30))),
            }
        report["comparisons"][MODELS[model]] = comparison
    (output / "realism-review.json").write_text(json.dumps(report, indent=2) + "\n")

    def button(stage, model, amount, route, label):
        key = f"{stage}_model_{model}_artifacts_{amount}_{route}"
        return (f'<button type="button" data-clip="{key}" aria-pressed="false">'
                f'{html.escape(label)}</button>')

    cards = []
    for model, name in enumerate(MODELS):
        artifacts = "".join(button("after", model, amount, "close", f"New {amount}%")
                            for amount in (0, 50, 100))
        artifacts += button("before", model, 100, "close", "Previous 100%")
        pickup = "".join(button(stage, model, 35, route, f"{label} {route.title()}")
                         for stage, label in (("after", "New"), ("before", "Previous"))
                         for route in ("close", "piezo"))
        cards.append(f'<section><h2>{html.escape(name)}</h2>'
                     f'<p>Artifacts through Close</p><div class="choices">{artifacts}</div>'
                     f'<p>Pickup comparison · Artifacts 35%</p><div class="choices">{pickup}</div></section>')
    page = '''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Maremba engine realism review</title>
<style>:root{font:16px/1.5 system-ui;color:#eae9e2;background:#181c1b;color-scheme:dark}
*{box-sizing:border-box}body{max-width:1000px;margin:auto;padding:28px 22px 80px}h1{line-height:1.2}
p{color:#bdc5c0}main{display:grid;grid-template-columns:1fr 1fr;gap:16px}section{padding:20px;border:1px solid #43514a;border-radius:10px}
h2{font-size:20px;margin:0}section p{font-size:14px;margin:20px 0 8px}.choices{display:flex;flex-wrap:wrap;gap:8px}
button{padding:9px 12px;border:1px solid #627569;border-radius:5px;background:#2b3930;color:inherit;font:inherit;font-size:14px;cursor:pointer}
button[aria-pressed=true]{background:#e2c38c;color:#20231e}button:focus-visible{outline:3px solid #8fc9f3;outline-offset:2px}
.player{position:sticky;top:0;padding:14px 0;background:#181c1bf5;margin:20px 0;z-index:2}audio{width:100%;margin:8px 0}
label,a{font-size:14px}a{color:#e2c38c}#status{font-weight:600}#download{margin-left:15px}
@media(max-width:680px){main{grid-template-columns:1fr}.player{position:static}}</style></head><body>
<h1>Maremba · engine realism</h1>
<p>Start with New 0%, then 50% and 100% to hear how the same strikes change. Compare New Piezo with New Close to hear the pickup mounted on the instrument. “Previous” uses the saved engine from before this change.</p>
<p>Every 10-second take plays four identical C4 strikes, then eight notes spanning C3–C5 at different velocities. All clips use the same fixed playback gain, with no loudness matching. Close is stereo; Piezo is mono. Room, preamp, limiter, Buzz, Bloom and sympathetic resonance are off; Variance and extra velocity routes are zero.</p>
<div class="player"><div id="status" aria-live="polite">Choose a comparison to play</div>
<audio id="player" controls preload="metadata"></audio><label><input id="keep-time" type="checkbox" checked> Keep position when switching</label>
<a id="download" hidden download>Download selected WAV</a></div><main>'''
    page += "".join(cards)
    page += (f'</main><p>Shared render gain: {args.gain:g}× ({db(args.gain):+.2f} dB). '
             '<a href="realism-review.json">Measurements, score and source hashes</a>. '
             'These are native engine renders, not a Reason host recording.</p><script>const clips=')
    page += json.dumps(report["clips"])
    page += ''';const player=document.querySelector('#player');const status=document.querySelector('#status');
let pending=0;document.querySelectorAll('[data-clip]').forEach(button=>button.onclick=()=>{
const clip=clips[button.dataset.clip];const time=document.querySelector('#keep-time').checked?player.currentTime:0;
const request=++pending;player.pause();player.src=clip.file;
document.querySelectorAll('[data-clip]').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));
status.textContent=clip.model+' · '+button.textContent+' · Artifacts '+clip.artifacts_percent+'%';
const link=document.querySelector('#download');link.href=clip.file;link.hidden=false;
player.onloadedmetadata=()=>{if(request!==pending)return;player.currentTime=time<player.duration?time:0;
player.play().catch(()=>{status.textContent+=' · press Play to start';});};player.load();
});</script></body></html>'''
    (output / "realism-review.html").write_text(page)
    print(output / "realism-review.html")
    print(json.dumps({"common_playback_gain": args.gain, "clips": len(report["clips"]),
                      "comparisons": report["comparisons"]}, indent=2))


if __name__ == "__main__":
    main()
