#!/usr/bin/env python3
"""Package render_tone_review's matched float renders into a standalone player.

Usage: python3 Tools/Demos/build_tone_review.py Release/validation/character-rooms-20260922
Requires numpy. Input: before/*.f32, after/*.f32, interleaved stereo at 44.1 kHz.
"""
import base64
import hashlib
import io
import json
from pathlib import Path
import sys
import wave

import numpy as np

root = Path(sys.argv[1]).resolve()
rate = 44100
audio = {f"{stage}/{p.stem}": np.fromfile(p, dtype="<f4").reshape(-1, 2)
         for stage in ("before", "after") for p in sorted((root / stage).glob("*.f32"))}
assert audio and all(np.isfinite(a).all() for a in audio.values())
gain = min(2., .9 / max(float(np.abs(a).max()) for a in audio.values()))
clips = {}
for name, samples in audio.items():
    out = io.BytesIO()
    with wave.open(out, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(np.round(samples * gain * 32767).astype("<i2").tobytes())
    clips[name] = "data:audio/wav;base64," + base64.b64encode(out.getvalue()).decode("ascii")

def db_ratio(numerator, denominator):
    return round(float(10*np.log10(max(float(numerator), 1e-30)/max(float(denominator), 1e-30))), 3)

report = {"version": "1.3.0b1", "sample_rate": rate, "score": [48, 60, 67, 72],
          "velocity": .72, "preview_gain": gain, "controls": {}, "models": {},
          "input_sha256": {name: hashlib.sha256(a.tobytes()).hexdigest() for name, a in audio.items()}}
for control in ("artifacts", "bloom", "buzz", "warmth"):
    report["controls"][control] = {}
    for stage in ("before", "after"):
        low, high = (audio[f"{stage}/{control}_{step}"] for step in (0, 2))
        report["controls"][control][stage] = {"endpoint_difference_db_re_low": db_ratio(np.sum((high-low)**2), np.sum(low**2))}
for stage in ("before", "after"):
    for model, label in ((0, "Rosewood"), (1, "Padauk")):
        a = audio[f"{stage}/model_{model}"]
        report["models"][f"{stage}/{label}"] = {
            "first_note_late_to_early_rms_db": db_ratio(np.mean(a[8820:22050]**2), np.mean(a[:4410]**2))}
(root / "tone-review.json").write_text(json.dumps(report, indent=2) + "\n")

groups = [
    ("Two woods, two voices", "Same dry settings. Padauk has a shorter, woodier ring and stronger upper partials.",
     [("Rosewood", "after/model_0"), ("Padauk", "after/model_1"), ("Previous Padauk", "before/model_1")]),
    ("Three spaces", "Room at 80%. Wood: the original comb room. Chamber: a short feedback-delay network. Hall: a longer allpass tank.",
     [("Wood", "after/room_0"), ("Chamber", "after/room_1"), ("Hall", "after/room_2")]),
    ("Artifacts", "Contact noise and cord rattle. Changes apply to the next strike.",
     [("Off", "after/artifacts_0"), ("50%", "after/artifacts_1"), ("100%", "after/artifacts_2"), ("Previous 100%", "before/artifacts_2")]),
    ("Bloom", "Frame resonance, heard through Close and Room. Play low notes to expose the body of the instrument.",
     [("Off", "after/bloom_0"), ("50%", "after/bloom_1"), ("100%", "after/bloom_2"), ("Previous 100%", "before/bloom_2")]),
    ("Buzz", "Membrane rasp. The control also responds while notes are ringing.",
     [("Off", "after/buzz_0"), ("50%", "after/buzz_1"), ("100%", "after/buzz_2"), ("Previous 100%", "before/buzz_2")]),
    ("Warmth", "Main outputs only. Cool to warm now spans a wider, smoothed tilt EQ.",
     [("Cool", "after/warmth_0"), ("Neutral", "after/warmth_1"), ("Warm", "after/warmth_2"), ("Previous warm", "before/warmth_2")]),
]
cards = "".join(f'<section><h2>{title}</h2><p>{description}</p><div class="choices">' +
                "".join(f'<button data-clip="{clip}">{label}</button>' for label, clip in buttons) + '</div></section>'
                for title, description, buttons in groups)
selected = {clip: clips[clip] for _, _, buttons in groups for _, clip in buttons}
html = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Maremba · sound review</title><style>
:root{color-scheme:dark;font:16px/1.55 system-ui;background:#171b18;color:#eee9d9}*{box-sizing:border-box}
body{max-width:980px;margin:auto;padding:40px 28px 120px}h1{font-size:42px;line-height:1.15;margin:12px 0}h2{font-size:21px;margin:0}
.eyebrow{color:#d4b58a;letter-spacing:.14em;font-size:12px}p{color:#c1c4b9;max-width:760px}main{display:grid;grid-template-columns:1fr 1fr;gap:18px}
section{padding:24px;border:1px solid #424a40;border-radius:16px;background:#20261f}section p{font-size:14px;min-height:66px}
button{cursor:pointer;background:#323e31;color:#f3eedf;border:1px solid #66705d;border-radius:8px;padding:10px 13px;font:inherit;font-size:14px}
button[aria-pressed=true]{background:#ddc299;color:#1b2419;border-color:#ddc299}button:focus-visible{outline:3px solid #69b7ff;outline-offset:3px}
.choices{display:flex;flex-wrap:wrap;gap:8px}footer{position:fixed;bottom:0;left:0;right:0;background:#101510f2;border-top:1px solid #46503e;padding:15px 24px}
.transport{max-width:924px;margin:auto;display:flex;gap:16px;align-items:center}#status{flex:1;font-size:14px}input{width:160px;accent-color:#dfc496}
@media(max-width:650px){main{grid-template-columns:1fr}h1{font-size:32px}.transport{flex-wrap:wrap}section p{min-height:0}}
</style><body><div class="eyebrow">PROTOCODUS / MAREMBA 1.3.0b1</div><h1>A little more character.</h1>
<p>Compare the revised voices, spaces and character controls. Each clip uses the same four-note phrase, 72% velocity and shared playback gain. These are native DSP renders of Main L/R. Effects are isolated to make each change easy to hear.</p>
<main>''' + cards + '''</main><footer><div class="transport"><button id="pause">Pause</button><button id="restart">Restart</button>
<span id="status" aria-live="polite">Choose a sound to play</span><label>Level <input id="volume" aria-label="Playback level" type="range" min="0" max="1" step=".01" value=".7"></label></div></footer>
<script>const clips=''' + json.dumps(selected) + ''';const player=new Audio();player.volume=.7;let active=null;
const status=document.querySelector('#status');const pause=document.querySelector('#pause');
document.querySelectorAll('[data-clip]').forEach(button=>{button.setAttribute('aria-pressed','false');button.onclick=()=>{
const position=player.paused?0:player.currentTime;active=button;player.src=clips[button.dataset.clip];player.currentTime=position;
document.querySelectorAll('[data-clip]').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));
status.textContent=button.closest('section').querySelector('h2').textContent+' · '+button.textContent;
player.play().then(()=>pause.textContent='Pause').catch(()=>status.textContent='Press Play to start audio');}});
pause.onclick=()=>{if(!active)return;if(player.paused){player.play();pause.textContent='Pause'}else{player.pause();pause.textContent='Play'}};
document.querySelector('#restart').onclick=()=>{if(active){player.currentTime=0;player.play();pause.textContent='Pause'}};
document.querySelector('#volume').oninput=e=>player.volume=Number(e.target.value);player.onended=()=>pause.textContent='Play';</script></body></html>'''
(root / "sound-review.html").write_text(html)
print(root / "sound-review.html")
print(json.dumps({k: report[k] for k in ("controls", "models")}, indent=2))
