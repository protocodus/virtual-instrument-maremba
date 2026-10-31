#!/usr/bin/env python3
"""Build one self-contained HTML page for a blind listening test (ABX or MUSHRA).

  python3 Tools/Demos/build_listening_test.py --demo --mode mushra --out demo.html
  python3 Tools/Demos/build_listening_test.py --trials trials.json --mode abx --abx-repeats 8 --seed 7 --out abx.html
  python3 Tools/Demos/build_listening_test.py --trial rec_C4.wav maremba=render_C4.wav other=comp_C4.wav --out t.html

trials.json (paths relative to the file; "file" alone takes its stem as label;
titles and metadata are visible in the page, labels are not):
  {"title": "Maremba vs recordings", "notice": "credits shown in the page footer",
   "trials": [{"title": "C4", "metadata": {"note": "C4"},
               "reference": {"file": "rec/ROS_C4_v4.wav", "label": "recording"},
               "candidates": [{"file": "render/C4.wav", "label": "Maremba 1.3"},
                              {"file": "comp/C4.wav", "label": "competitor"}]}]}
A clip may carry "transform": {"cents": 15, "lowpass_hz": 2500} for a diagnostic
variant (pitch shift by resampling, 2nd-order low-pass), as the demo does.

Every clip of a trial is resampled to one rate, given one channel count, aligned
on its detected onset (--preroll before it), cut to one duration with a short
fade, and normalised to one integrated loudness (ITU-R BS.1770 K-weighting and
gating), lowering the whole trial if a peak would pass -1 dBFS. MUSHRA adds a
hidden reference and a 3.5 kHz low-pass anchor. Order, A/B assignment and X are
drawn from --seed. Every playable slot is its own separately dithered encoding,
so identical audio never shares identical bytes. Audio is AAC (ffmpeg) when
available, else 16-bit WAV. Labels are stored only obfuscated (XOR keystream;
obfuscation, not encryption) and appear in the exported results, never in the
page; answers changed after an export are flagged. A report next to the page
lists the loudness gains and the label key: do not send it to listeners.
Requires numpy and scipy; ffmpeg optional.
"""
from __future__ import annotations

import os

for _variable in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ.setdefault(_variable, "1")

import argparse  # noqa: E402
import atexit  # noqa: E402
import base64  # noqa: E402
import datetime  # noqa: E402
import hashlib  # noqa: E402
import json  # noqa: E402
import math  # noqa: E402
import shutil  # noqa: E402
import subprocess  # noqa: E402
import sys  # noqa: E402
import tempfile  # noqa: E402
from fractions import Fraction  # noqa: E402
from pathlib import Path  # noqa: E402

import numpy as np  # noqa: E402
from scipy import signal  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fit_modal_reference as fmr  # noqa: E402

TOOL = "build_listening_test.py"
VERSION = "1.0.0"
DEMOS_DIR = Path(__file__).resolve().parent
PEAK_CEILING_DB = -1.0
FADE_S = 0.05
DEMO_NOTICE = ("Demo audio: synthesized modal strikes (fit_modal_reference.synthesize_case), not recordings.")


# ─────────────────────────────── loudness (BS.1770) ───────────────────────────────

def k_weighting(rate):
    """BS.1770 K-weighting (high shelf + RLB high-pass) as second-order sections for
    any rate; libebur128's closed form, equal to the ITU 48 kHz coefficients."""
    f0, gain_db, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / rate)
    vh = 10 ** (gain_db / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    shelf = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
             1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    f0, q = 38.13547087602444, 0.5003270373238773
    k = math.tan(math.pi * f0 / rate)
    a0 = 1 + k / q + k * k
    highpass = [1.0, -2.0, 1.0, 1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    return np.array([shelf, highpass])


def integrated_loudness(x, rate):
    """Gated integrated loudness in LUFS (400 ms blocks, 75 % overlap, -70 LUFS
    absolute and -10 LU relative gates; unit weight for each of up to two channels).
    Clips shorter than one block use the ungated mean square."""
    x = np.atleast_2d(np.asarray(x, dtype=np.float64).T).T
    y = signal.sosfilt(k_weighting(rate), x, axis=0)
    block, step = int(round(0.4 * rate)), int(round(0.1 * rate))
    if len(y) < block:
        power = float(np.sum(np.mean(y * y, axis=0)))
        return -0.691 + 10 * math.log10(power) if power > 0 else -math.inf
    starts = np.arange(0, len(y) - block + 1, step)
    power = np.array([np.sum(np.mean(y[s:s + block] ** 2, axis=0)) for s in starts])
    with np.errstate(divide="ignore"):
        loud = -0.691 + 10 * np.log10(power)
    gated = power[loud > -70.0]
    if len(gated) == 0:
        return -math.inf
    relative = -0.691 + 10 * math.log10(np.mean(gated)) - 10.0
    gated = power[(loud > -70.0) & (loud > relative)]
    return -0.691 + 10 * math.log10(np.mean(gated))


# ─────────────────────────────── clip preparation ───────────────────────────────

def resample(x, fs, rate):
    if fs == rate:
        return x
    ratio = Fraction(rate, fs).limit_denominator(2000)
    return signal.resample_poly(x, ratio.numerator, ratio.denominator, axis=0)


def transform(x, rate, spec):
    """Diagnostic variants: {"cents": pitch shift by resampling, "lowpass_hz": 2nd-order low-pass}."""
    if spec.get("cents"):
        factor = 2 ** (spec["cents"] / 1200)
        x = signal.resample(x, int(round(len(x) / factor)), axis=0)
    if spec.get("lowpass_hz"):
        x = signal.sosfilt(signal.butter(2, spec["lowpass_hz"], fs=rate, output="sos"), x, axis=0)
    return x


def lowpass_anchor(x, rate, cutoff=3500.0):
    """The MUSHRA low-pass anchor (BS.1534): steep causal 8th-order Butterworth."""
    return signal.sosfilt(signal.butter(8, cutoff, fs=rate, output="sos"), x, axis=0)


def conform(clips, rate, preroll, duration):
    """Common channel count, onset alignment, length and end fade for one trial."""
    channels = 2 if any(c.shape[1] >= 2 for c in clips) else 1
    onsets = [fmr.detect_onset(c.mean(axis=1), rate) for c in clips]
    if duration is None:
        duration = min(4.0, max((len(c) - o) / rate for c, o in zip(clips, onsets)) + preroll)
    length, pre = int(round(duration * rate)), int(round(preroll * rate))
    fade = np.cos(0.5 * np.pi * np.linspace(0, 1, int(FADE_S * rate))) ** 2
    out = []
    for clip, onset in zip(clips, onsets):
        if clip.shape[1] > 2:
            clip = clip.mean(axis=1, keepdims=True)
        if clip.shape[1] != channels:
            clip = np.repeat(clip[:, :1], channels, axis=1)
        y = np.zeros((length, channels))
        start = onset - pre
        src = clip[max(0, start):max(0, start) + length - max(0, -start)]
        y[max(0, -start):max(0, -start) + len(src)] = src
        y[-len(fade):] *= fade[:, None]
        out.append(y)
    return out, duration


def encode(samples, rate, audio, dither_seed, workdir):
    """A data URI for one slot: TPDF-dithered 16-bit WAV, or AAC via ffmpeg."""
    wav = Path(workdir) / f"slot_{dither_seed}.wav"
    fmr.write_wav(wav, samples, rate, "pcm16", dither_seed=dither_seed)
    if audio == "wav":
        return "data:audio/wav;base64," + base64.b64encode(wav.read_bytes()).decode("ascii")
    m4a = wav.with_suffix(".m4a")
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav), "-map_metadata", "-1",
                    "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", str(m4a)], check=True)
    decoded = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(m4a), "-f", "f32le",
                              "-acodec", "pcm_f32le", "-"], check=True, capture_output=True).stdout
    peak = float(np.max(np.abs(np.frombuffer(decoded, "<f4")))) if decoded else 0.0
    if not 0 < peak < 1.0:
        raise RuntimeError(f"AAC reconstruction peak {peak:.3f} is silent or clips")
    return "data:audio/mp4;base64," + base64.b64encode(m4a.read_bytes()).decode("ascii")


def seal(obj, key):
    """XOR the JSON with a xorshift32 keystream (mirrored in the page's JavaScript)."""
    data, state, out = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode(), key, bytearray()
    for byte in data:
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        out.append(byte ^ (state & 255))
    return base64.b64encode(bytes(out)).decode("ascii")


# ─────────────────────────────── trial specification ───────────────────────────────

def clip_spec(spec, base, default_label=None):
    if isinstance(spec, str):
        spec = {"file": spec}
    path = Path(spec["file"])
    path = path if path.is_absolute() else Path(base) / path
    if not path.is_file():
        raise FileNotFoundError(path)
    return {"file": path, "label": spec.get("label") or default_label or path.stem,
            "transform": spec.get("transform") or {}}


def trials_from_json(path):
    data = json.loads(Path(path).read_text())
    base = Path(path).resolve().parent
    entries = data["trials"] if isinstance(data, dict) else data
    trials = [{"title": t.get("title"), "metadata": t.get("metadata") or {},
               "reference": clip_spec(t["reference"], base, "reference"),
               "candidates": [clip_spec(c, base) for c in t["candidates"]]} for t in entries]
    head = data if isinstance(data, dict) else {}
    return trials, head.get("title"), head.get("notice")


def trials_from_cli(groups):
    trials = []
    for group in groups:
        if len(group) < 2:
            raise ValueError("--trial needs a reference and at least one candidate")
        specs = []
        for token in group:
            label, sep, path = token.partition("=")
            specs.append({"file": path, "label": label} if sep and not Path(token).is_file() else {"file": token})
        trials.append({"title": None, "metadata": {}, "reference": clip_spec(specs[0], Path.cwd(), "reference"),
                       "candidates": [clip_spec(s, Path.cwd()) for s in specs[1:]]})
    return trials


# Two synthesized marimba-like strikes per note stand in for recordings, so the
# demo needs no sample set: "a" rings like a tuned rosewood bar under its tube,
# "b" is drier and brighter (the stand-in for a second instrument).
DEMO_VOICES = {
    "a": {"ratios": (1.0, 4.0, 9.9), "t60": (1.2, 0.35, 0.15), "levels_db": (0.0, -9.0, -17.0), "bloom": (0.9, 0.04)},
    "b": {"ratios": (1.0, 3.98, 9.7), "t60": (0.8, 0.30, 0.12), "levels_db": (0.0, -5.0, -11.0)},
}


def demo_trials():
    ref = Path(tempfile.mkdtemp(prefix="maremba-demo-"))
    atexit.register(shutil.rmtree, ref, True)
    for index, (note, voice) in enumerate([(n, v) for n in ("C4", "C5", "G4") for v in DEMO_VOICES]):
        case = {"note": note, "cents": 0.0, **DEMO_VOICES[voice]}
        x, *_ = fmr.synthesize_case(case, 48000, seed=2000 + index, seconds=2.0)
        fmr.write_wav(ref / f"demo-{voice}-{note}.wav", x[:, None], 48000, "pcm16", dither_seed=7)
    variant = {"cents": 15, "lowpass_hz": 2500}
    trials = []
    for note, other in (("C4", True), ("C5", True), ("G4", False)):
        candidates = [{"file": str(ref / f"demo-a-{note}.wav"), "label": "variant: +15 cents, 2.5 kHz low-pass",
                       "transform": variant}]
        if other:
            candidates.insert(0, {"file": str(ref / f"demo-b-{note}.wav"), "label": "drier, brighter strike"})
        trials.append({"title": note, "metadata": {"note": note},
                       "reference": clip_spec({"file": str(ref / f"demo-a-{note}.wav"),
                                               "label": "rosewood-like strike"}, ref),
                       "candidates": [clip_spec(c, ref) for c in candidates]})
    return trials


# ─────────────────────────────── build ───────────────────────────────

def build(trials, mode, seed, out, audio, rate, preroll, duration, target_lufs, repeats, title, notice, log):
    rng = np.random.default_rng(seed)
    digest = hashlib.sha256(f"{TOOL} {VERSION} {mode} {seed} {rate} {preroll} {duration} {target_lufs} "
                            f"{repeats} {audio}".encode())
    for trial in trials:
        for clip in (trial["reference"], *trial["candidates"]):
            digest.update(hashlib.sha256(clip["file"].read_bytes()).digest())
            digest.update(json.dumps(clip["transform"], sort_keys=True).encode())
    build_id = digest.hexdigest()[:12]
    key = int(rng.integers(1, 2 ** 32 - 1))
    used_ids = set()

    def new_id():
        while True:
            value = "s" + "".join(rng.choice(list("0123456789abcdef"), 8))
            if value not in used_ids:
                used_ids.add(value)
                return value

    page_trials, sealed, report_trials, slots_audio = [], {}, [], {}
    cache = {}
    for number, trial in enumerate(trials, start=1):
        loaded = []
        for clip in (trial["reference"], *trial["candidates"]):
            if clip["file"] not in cache:
                x, fs, _ = fmr.read_wav(clip["file"])
                cache[clip["file"]] = resample(x, fs, rate)
            loaded.append(transform(cache[clip["file"]], rate, clip["transform"]))
        conformed, used_duration = conform(loaded, rate, preroll, duration)
        roles = ["reference"] + ["candidate"] * len(trial["candidates"])
        labels = [trial["reference"]["label"]] + [c["label"] for c in trial["candidates"]]
        sources = [trial["reference"]["file"].name] + [c["file"].name for c in trial["candidates"]]
        if mode == "mushra":
            conformed.append(lowpass_anchor(conformed[0], rate))
            roles.append("anchor")
            labels.append("3.5 kHz low-pass anchor")
            sources.append(sources[0])
        # Loudness matching, then one common reduction if any peak would pass the ceiling.
        loudness = [integrated_loudness(c, rate) for c in conformed]
        gains = [target_lufs - lufs for lufs in loudness]
        peaks = [20 * math.log10(np.max(np.abs(c))) + g for c, g in zip(conformed, gains)]
        offset = min(0.0, PEAK_CEILING_DB - max(peaks))
        levelled = [c * 10 ** ((g + offset) / 20) for c, g in zip(conformed, gains)]
        trial_id = f"t{number}"
        clips_report = [{"role": r, "label": lab, "file": str(clip["file"]), "transform": clip["transform"] or None,
                         "loudness_lufs": round(lufs, 2), "gain_db": round(g + offset, 2),
                         "final_lufs": round(lufs + g + offset, 2), "peak_dbfs": round(p + offset, 2)}
                        for r, lab, clip, lufs, g, p in zip(
                            roles, labels, [trial["reference"], *trial["candidates"], trial["reference"]],
                            loudness, gains, peaks)]
        base = {"title": trial["title"] or f"Trial {number}", "metadata": trial["metadata"]}
        if mode == "mushra":
            def add(index, role):
                sid = new_id()
                slots_audio[sid] = levelled[index]
                sealed.setdefault(trial_id, {"slots": {}})["slots"][sid] = {
                    "label": labels[index], "role": role, "source": sources[index]}
                return sid
            reference = add(0, "reference")
            conditions = [add(0, "hidden reference"), add(len(levelled) - 1, "anchor")]
            conditions += [add(i, "candidate") for i in range(1, len(levelled) - 1)]
            conditions = [conditions[i] for i in rng.permutation(len(conditions))]
            page_trials.append(dict(base, id=trial_id, kind="mushra", reference=reference, conditions=conditions))
            report_trials.append(dict(base, id=trial_id, duration_s=round(used_duration, 3), trial_offset_db=round(offset, 2),
                                      clips=clips_report, slots=[{"slot": s, **sealed[trial_id]["slots"][s]}
                                                                 for s in [reference, *conditions]]))
        else:
            report_trials.append(dict(base, id=trial_id, duration_s=round(used_duration, 3),
                                      trial_offset_db=round(offset, 2), clips=clips_report, abx=[]))
            for candidate in range(1, len(levelled)):
                for repeat in range(repeats):
                    abx_id = f"{trial_id}-{candidate}-{repeat + 1}"
                    swap = bool(rng.integers(2))
                    order = (candidate, 0) if swap else (0, candidate)
                    x_is = "AB"[int(rng.integers(2))]
                    ids = {name: new_id() for name in "abx"}
                    slot_meta = {}
                    for name, index in zip("abx", (*order, order[0] if x_is == "A" else order[1])):
                        slots_audio[ids[name]] = levelled[index]
                        slot_meta[ids[name]] = {"label": labels[index], "role": roles[index], "source": sources[index]}
                    sealed[abx_id] = {"slots": slot_meta, "x_is": x_is, "candidate": labels[candidate],
                                      "reference": labels[0]}
                    page_trials.append(dict(base, id=abx_id, kind="abx", a=ids["a"], b=ids["b"], x=ids["x"]))
                    report_trials[-1]["abx"].append({"id": abx_id, "candidate": labels[candidate],
                                                     "a": labels[order[0]], "b": labels[order[1]], "x_is": x_is})
    page_trials = [page_trials[i] for i in rng.permutation(len(page_trials))]

    clips = {}
    with tempfile.TemporaryDirectory() as workdir:
        for sid, samples in slots_audio.items():
            clips[sid] = encode(samples, rate, audio, int(rng.integers(1, 2 ** 31 - 1)), workdir)

    built = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()
    data = {"build": {"id": build_id, "title": title, "mode": mode, "seed": seed, "built": built,
                      "tool": f"{TOOL} {VERSION}", "audio": audio, "rate": rate, "target_lufs": target_lufs,
                      "abx_repeats": repeats if mode == "abx" else None, "trials": len(page_trials),
                      "clips": len(clips)},
            "trials": page_trials, "key": key, "sealed": seal(sealed, key)}
    html = page_html(data, clips, title, notice)
    out = Path(out)
    out.write_text(html)
    report = {"tool": f"{TOOL} {VERSION}", "built": built, "build_id": build_id, "html": str(out), "mode": mode,
              "seed": seed, "audio": audio, "rate": rate, "target_lufs": target_lufs, "clips_embedded": len(clips),
              "html_bytes": len(html.encode()), "trials": report_trials}
    report_path = out.with_name(out.stem + ".report.json")
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    for trial in report_trials:
        log(f"{trial['id']} {trial['title']}: {trial['duration_s']:.2f} s"
            + (f", whole trial lowered {trial['trial_offset_db']:.1f} dB for peaks" if trial["trial_offset_db"] else ""))
        for clip in trial["clips"]:
            log(f"   {clip['role']:<10} {clip['loudness_lufs']:7.2f} LUFS  gain {clip['gain_db']:+6.2f} dB  "
                f"-> {clip['final_lufs']:6.2f} LUFS, peak {clip['peak_dbfs']:6.2f} dBFS  {clip['label']}")
    log(f"{out}: {len(page_trials)} {mode.upper()} trials, {len(clips)} clips ({audio}), "
        f"{len(html.encode()) / 1e6:.1f} MB; key and gains in {report_path.name} (keep it from listeners)")
    return report


def json_for_script(obj):
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c").replace(
        ">", "\\u003e").replace("&", "\\u0026")


def page_html(data, clips, title, notice):
    from html import escape
    instructions = {
        "mushra": "Play the reference, then rate how close each sample sounds to it, from 0 (bad) to 100 "
                  "(excellent). One sample is the reference itself: give it 100. Keys: R reference, 1–9 samples, "
                  "space stop.",
        "abx": "A and B are two different sounds; X is one of them. Listen as often as you like and decide whether "
               "X is A or B. Keys: A, B, X to play, space to stop.",
    }[data["build"]["mode"]]
    return (PAGE.replace("__TITLE__", escape(title))
            .replace("__MODE__", "MUSHRA" if data["build"]["mode"] == "mushra" else "ABX")
            .replace("__INSTRUCTIONS__", escape(instructions))
            .replace("__NOTICE__", escape(notice or ""))
            .replace("__DATA__", json_for_script(data))
            .replace("__CLIPS__", json_for_script(clips)))


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="icon" href="data:,">
<title>__TITLE__</title>
<style>
:root{color-scheme:dark;--bg:#171b18;--panel:#20261f;--line:#424a40;--text:#eee9d9;--muted:#c1c4b9;--accent:#ddc299;--button:#323e31;--focus:#69b7ff;--ok:#8fc98a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.55 system-ui,-apple-system,"Segoe UI",sans-serif}
.wrap{max-width:920px;margin:0 auto;padding:32px 16px 48px}
.eyebrow{color:var(--accent);letter-spacing:.14em;font-size:12px;text-transform:uppercase}
h1{font-size:34px;line-height:1.15;margin:8px 0 10px}
h2{font-size:21px;margin:0 0 4px}
p{color:var(--muted);margin:0 0 12px}
section{padding:22px;border:1px solid var(--line);border-radius:14px;background:var(--panel);margin-top:18px}
button{cursor:pointer;background:var(--button);color:var(--text);border:1px solid #66705d;border-radius:8px;padding:9px 14px;font:inherit;font-size:15px}
button:hover{border-color:var(--accent)}
button[aria-pressed=true]{background:var(--accent);color:#1b2419;border-color:var(--accent)}
button:disabled{opacity:.45;cursor:default}
:focus-visible{outline:3px solid var(--focus);outline-offset:3px}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
.progress{height:6px;border-radius:3px;background:#2d352c;overflow:hidden;margin:12px 0 4px}
.progress>div{height:100%;background:var(--accent);width:0}
.meta{font-size:14px;color:var(--muted)}
.big button.play{min-width:92px;font-size:18px;padding:14px 18px}
ol.conditions{list-style:none;margin:16px 0 0;padding:0}
ol.conditions li{display:grid;grid-template-columns:72px 1fr 44px;gap:12px;align-items:center;padding:8px 0;border-top:1px solid #333b31}
ol.conditions li.rated output{color:var(--ok)}
.scale{display:grid;grid-template-columns:repeat(5,1fr);font-size:12px;color:var(--muted);text-align:center;margin-left:84px;margin-right:56px}
input[type=range]{width:100%;accent-color:var(--accent)}
output{font-variant-numeric:tabular-nums;text-align:right}
.choice{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
.choice label{display:flex;gap:8px;align-items:center;padding:10px 14px;border:1px solid var(--line);border-radius:8px;cursor:pointer}
.choice input{accent-color:var(--accent)}
label.field{display:block;font-size:14px;color:var(--muted);margin-bottom:10px}
label.field input,label.field textarea{display:block;width:100%;margin-top:4px;background:#11150f;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px;font:inherit}
#status{min-height:1.5em;font-size:14px;color:var(--accent)}
footer{margin-top:28px;font-size:12px;color:#8e9487}
@media(max-width:560px){h1{font-size:27px}ol.conditions li{grid-template-columns:56px 1fr 40px}.scale{margin-left:68px;margin-right:48px}}
</style>
</head>
<body>
<div class="wrap">
<div class="eyebrow">Protocodus / Maremba · blind __MODE__ test</div>
<h1>__TITLE__</h1>
<p>__INSTRUCTIONS__</p>
<noscript><p>This test needs JavaScript.</p></noscript>
<div class="progress" aria-hidden="true"><div id="bar"></div></div>
<div class="meta" id="progress-text"></div>
<section id="app" aria-live="polite"></section>
<section>
<div class="row"><button id="stop">Stop</button><label class="row"><input type="checkbox" id="loop" checked> Loop</label>
<label class="row">Level <input type="range" id="volume" min="0" max="1" step="0.01" value="0.8" aria-label="Playback level" style="width:140px"></label></div>
</section>
<section>
<h2>Your results</h2>
<p>Answers are kept in this browser. When you are done, export them and send the file.</p>
<label class="field">Name or code <input type="text" id="listener" autocomplete="off"></label>
<label class="field">Comments <textarea id="comments" rows="3"></textarea></label>
<div class="row"><button id="export">Export results (JSON)</button></div>
<div id="status" role="status"></div>
</section>
<footer>__NOTICE__</footer>
</div>
<script type="application/json" id="test-data">__DATA__</script>
<script type="application/json" id="test-clips">__CLIPS__</script>
<script>
(function () {
'use strict';
const DATA = JSON.parse(document.getElementById('test-data').textContent);
const CLIPS = JSON.parse(document.getElementById('test-clips').textContent);
const STORE = 'maremba-listening:' + DATA.build.id;
const $ = (s) => document.querySelector(s);
const app = $('#app');
let state = restore() || {listener: '', comments: '', index: 0, exports: 0, responses: {}};

function restore() { try { const s = localStorage.getItem(STORE); return s ? JSON.parse(s) : null; } catch (e) { return null; } }
function persist() { try { localStorage.setItem(STORE, JSON.stringify(state)); } catch (e) { /* memory only */ } }
function say(text) { $('#status').textContent = text; }
function el(tag, attrs, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) node.setAttribute(k, v);
  for (const c of children) node.append(c);
  return node;
}

// ---- audio: one context, seamless switching at the same position, 10 ms crossfades ----
let ctx = null, master = null, current = null;
const buffers = new Map();
function bytes(uri) {
  const bin = atob(uri.slice(uri.indexOf(',') + 1));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out.buffer;
}
async function audio() {
  if (!ctx) {
    ctx = new (window.AudioContext || window.webkitAudioContext)();
    master = ctx.createGain();
    master.gain.value = Number($('#volume').value);
    master.connect(ctx.destination);
  }
  if (ctx.state === 'suspended') await ctx.resume();
}
async function decoded(id) {
  if (!buffers.has(id)) buffers.set(id, await ctx.decodeAudioData(bytes(CLIPS[id])));
  return buffers.get(id);
}
function release(entry, when) {
  const g = entry.gain.gain;
  g.cancelScheduledValues(when); g.setValueAtTime(g.value, when); g.linearRampToValueAtTime(0, when + 0.01);
  entry.source.onended = null; entry.source.stop(when + 0.012);
}
async function play(id) {
  await audio();
  let buffer;
  try { buffer = await decoded(id); } catch (e) {
    say('This browser cannot decode the embedded ' + DATA.build.audio.toUpperCase() + ' audio; ask for a WAV build.');
    return;
  }
  const now = ctx.currentTime, loop = $('#loop').checked;
  let offset = current ? now - current.start : 0;
  if (current) release(current, now);
  offset = loop ? offset % buffer.duration : (offset < buffer.duration ? offset : 0);
  const gain = ctx.createGain();
  gain.gain.setValueAtTime(0, now); gain.gain.linearRampToValueAtTime(1, now + 0.01); gain.connect(master);
  const source = ctx.createBufferSource();
  source.buffer = buffer; source.loop = loop; source.connect(gain); source.start(now, offset);
  const entry = {id, source, gain, start: now - offset};
  source.onended = () => { if (current === entry) { current = null; highlight(null); } };
  current = entry; highlight(id); heard(id);
}
function stop() { if (current && ctx) { release(current, ctx.currentTime); current = null; highlight(null); } }
function highlight(id) { document.querySelectorAll('[data-slot]').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.slot === id))); }

// ---- responses ----
function trial() { return DATA.trials[state.index]; }
function response(t) { return state.responses[t.id] || (state.responses[t.id] = {played: [], started: new Date().toISOString()}); }
function heard(id) { const r = response(trial()); if (!r.played.includes(id)) { r.played.push(id); persist(); } }
function answered(r) { r.answered = new Date().toISOString(); if (state.exports > 0) r.after_export = true; persist(); progress(); }
function complete(t) {
  const r = state.responses[t.id];
  if (!r) return false;
  return t.kind === 'abx' ? Boolean(r.answer) : t.conditions.every((c) => r.ratings && r.ratings[c] !== undefined);
}
function progress() {
  const done = DATA.trials.filter(complete).length, n = DATA.trials.length;
  $('#bar').style.width = (100 * done / n) + '%';
  $('#progress-text').textContent = done + ' of ' + n + ' trials answered' + (done === n ? ' — thank you, please export your results.' : '');
}
const GRADES = ['bad', 'poor', 'fair', 'good', 'excellent'];
function grade(v) { return GRADES[Math.min(4, Math.floor(v / 20))]; }

function playButton(id, text, label) {
  const b = el('button', {class: 'play', 'data-slot': id, 'aria-pressed': 'false', 'aria-label': label || text}, text);
  b.addEventListener('click', () => play(id));
  return b;
}
function render() {
  stop();
  const t = trial(), n = DATA.trials.length;
  app.replaceChildren(el('h2', {}, 'Trial ' + (state.index + 1) + ' of ' + n + (t.title ? ' · ' + t.title : '')));
  if (t.kind === 'mushra') renderMushra(t); else renderAbx(t);
  const prev = el('button', {}, 'Previous'), next = el('button', {}, 'Next');
  prev.disabled = state.index === 0; next.disabled = state.index === n - 1;
  prev.addEventListener('click', () => go(-1)); next.addEventListener('click', () => go(1));
  app.append(el('div', {class: 'row', style: 'margin-top:18px'}, prev, next));
  progress();
}
function go(step) {
  const t = trial();
  if (step > 0 && !complete(t)) say(t.kind === 'abx' ? 'This trial has no answer yet.' : 'Some samples in this trial are not rated yet.');
  else say('');
  state.index = Math.max(0, Math.min(DATA.trials.length - 1, state.index + step)); persist(); render();
}
function renderMushra(t) {
  const r = response(t); r.ratings = r.ratings || {};
  app.append(el('div', {class: 'row big'}, playButton(t.reference, 'Reference')));
  const scale = el('div', {class: 'scale', 'aria-hidden': 'true'}, ...GRADES.map((g) => el('span', {}, g)));
  const list = el('ol', {class: 'conditions'});
  t.conditions.forEach((id, i) => {
    const value = r.ratings[id];
    const slider = el('input', {type: 'range', min: '0', max: '100', step: '1', value: String(value === undefined ? 50 : value),
                                'aria-label': 'Rating for sample ' + (i + 1)});
    const out = el('output', {}, value === undefined ? '–' : String(value));
    const row = el('li', {class: value === undefined ? '' : 'rated'}, playButton(id, String(i + 1), 'Play sample ' + (i + 1)), slider, out);
    slider.addEventListener('input', () => {
      r.ratings[id] = Number(slider.value); out.textContent = slider.value; row.className = 'rated';
      slider.setAttribute('aria-valuetext', slider.value + ', ' + grade(Number(slider.value)));
      answered(r);
    });
    list.append(row);
  });
  app.append(scale, list);
}
function renderAbx(t) {
  const r = response(t);
  app.append(el('div', {class: 'row big'}, playButton(t.a, 'A'), playButton(t.b, 'B'), playButton(t.x, 'X')));
  const choice = el('div', {class: 'choice', role: 'radiogroup', 'aria-label': 'X is'});
  for (const option of ['A', 'B']) {
    const input = el('input', {type: 'radio', name: 'x-' + t.id, value: option});
    input.checked = r.answer === option;
    input.addEventListener('change', () => { r.answer = option; answered(r); });
    choice.append(el('label', {}, input, 'X is ' + option));
  }
  app.append(choice);
}

// ---- export: labels are unsealed only into the downloaded file ----
function unseal() {
  const bin = atob(DATA.sealed), out = new Uint8Array(bin.length);
  let s = DATA.key >>> 0;
  for (let i = 0; i < bin.length; i++) {
    s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0;
    out[i] = bin.charCodeAt(i) ^ (s & 255);
  }
  return JSON.parse(new TextDecoder().decode(out));
}
function binomialTail(n, k) {
  let p = 0, c = 1;
  for (let i = 0; i <= n; i++) { if (i >= k) p += c * Math.pow(0.5, n); c = c * (n - i) / (i + 1); }
  return Math.min(1, p);
}
function stats(values) {
  const n = values.length, mean = values.reduce((a, b) => a + b, 0) / n;
  const sd = n > 1 ? Math.sqrt(values.reduce((a, b) => a + (b - mean) * (b - mean), 0) / (n - 1)) : null;
  return {n, mean: Math.round(mean * 10) / 10, sd: sd === null ? null : Math.round(sd * 10) / 10, min: Math.min(...values), max: Math.max(...values)};
}
function exportResults() {
  const key = unseal();
  state.exports += 1; persist();
  const describe = (k, id) => Object.assign({slot: id}, k.slots[id]);
  const button = (t, id) => t.kind === 'abx' ? ({[t.a]: 'A', [t.b]: 'B', [t.x]: 'X'})[id]
    : (id === t.reference ? 'reference' : 'sample ' + (t.conditions.indexOf(id) + 1));
  const trials = DATA.trials.map((t, i) => {
    const r = state.responses[t.id] || {}, k = key[t.id];
    const out = {order: i + 1, id: t.id, title: t.title, metadata: t.metadata, complete: complete(t),
                 started: r.started || null, answered: r.answered || null, answered_after_export: Boolean(r.after_export),
                 played: (r.played || []).map((id) => button(t, id))};
    if (t.kind === 'abx') {
      return Object.assign(out, {candidate: k.candidate, reference: k.reference, a: describe(k, t.a), b: describe(k, t.b),
                                 x_is: k.x_is, answer: r.answer || null, correct: r.answer ? r.answer === k.x_is : null});
    }
    return Object.assign(out, {reference: describe(k, t.reference), ratings: t.conditions.map((id, j) => Object.assign(
      {position: j + 1, rating: r.ratings && r.ratings[id] !== undefined ? r.ratings[id] : null}, describe(k, id)))});
  });
  const summary = {};
  if (DATA.build.mode === 'abx') {
    for (const t of trials) if (t.answer) {
      const s = summary[t.candidate] || (summary[t.candidate] = {versus: t.reference, trials: 0, correct: 0});
      s.trials += 1; if (t.correct) s.correct += 1;
    }
    for (const s of Object.values(summary)) s.p_value_one_sided = Number(binomialTail(s.trials, s.correct).toPrecision(4));
  } else {
    const groups = {};
    for (const t of trials) for (const r of t.ratings) if (r.rating !== null) {
      const name = r.role === 'candidate' ? r.label : r.role;
      (groups[name] = groups[name] || []).push(r.rating);
    }
    for (const [name, values] of Object.entries(groups)) summary[name] = stats(values);
    const hidden = groups['hidden reference'] || [];
    summary.hidden_reference_below_90 = hidden.filter((v) => v < 90).length;
  }
  const result = {format: 'maremba-listening-results/1', test: DATA.build, listener: state.listener, comments: state.comments,
                  exported: new Date().toISOString(), export_number: state.exports, user_agent: navigator.userAgent,
                  trials, summary};
  const blob = new Blob([JSON.stringify(result, null, 2) + '\n'], {type: 'application/json'});
  const a = el('a', {href: URL.createObjectURL(blob),
                     download: 'listening-' + DATA.build.id + '-' + (state.listener || 'anonymous').replace(/[^\w-]+/g, '_') + '-' + state.exports + '.json'});
  document.body.append(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
  say('Exported. Answers you change from now on are marked as given after an export.');
}

$('#stop').addEventListener('click', stop);
$('#volume').addEventListener('input', (e) => { if (master) master.gain.value = Number(e.target.value); });
$('#loop').addEventListener('change', () => { if (current) current.source.loop = $('#loop').checked; });
$('#export').addEventListener('click', exportResults);
$('#listener').value = state.listener; $('#comments').value = state.comments;
$('#listener').addEventListener('input', (e) => { state.listener = e.target.value; persist(); });
$('#comments').addEventListener('input', (e) => { state.comments = e.target.value; persist(); });
document.addEventListener('keydown', (e) => {
  if (e.target.closest('input[type=text], textarea') || e.metaKey || e.ctrlKey || e.altKey) return;
  const t = trial(), k = e.key.toLowerCase();
  if (k === ' ' && !e.target.closest('button, input')) { e.preventDefault(); stop(); }
  else if (t.kind === 'abx' && (k === 'a' || k === 'b' || k === 'x')) play(t[k]);
  else if (t.kind === 'mushra' && k === 'r') play(t.reference);
  else if (t.kind === 'mushra' && /^[1-9]$/.test(k) && t.conditions[Number(k) - 1]) play(t.conditions[Number(k) - 1]);
});
state.index = Math.min(state.index, DATA.trials.length - 1);
render();
})();
</script>
</body>
</html>
"""


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--trials", metavar="JSON", help="trial list (see above)")
    source.add_argument("--trial", nargs="+", action="append", metavar="[LABEL=]WAV",
                        help="one trial: reference then candidates; repeat for more trials")
    source.add_argument("--demo", action="store_true", help="example page from synthesized strikes plus filtered variants")
    parser.add_argument("--mode", choices=("mushra", "abx"), default="mushra")
    parser.add_argument("--seed", type=int, default=1, help="randomisation seed (default 1)")
    parser.add_argument("--out", default="listening_test.html", help="output HTML (a .report.json is written beside it)")
    parser.add_argument("--format", choices=("auto", "aac", "wav"), default="auto",
                        help="embedded audio: AAC when ffmpeg is available (auto), or 16-bit WAV")
    parser.add_argument("--rate", type=int, default=48000, help="common sample rate (default 48000)")
    parser.add_argument("--preroll", type=float, default=0.03, help="seconds kept before each onset (default 0.03)")
    parser.add_argument("--duration", type=float, help="clip length in seconds (default: longest clip, at most 4 s; demo 2 s)")
    parser.add_argument("--target-lufs", type=float, default=-23.0, help="integrated loudness of every clip (default -23)")
    parser.add_argument("--abx-repeats", type=int, default=1, help="ABX trials per reference/candidate pair (default 1)")
    parser.add_argument("--title", help="page title")
    args = parser.parse_args(argv)

    notice = None
    if args.demo:
        trials, title, notice = demo_trials(), "Demo: two synthesized strikes and a variant", DEMO_NOTICE
        if args.duration is None:
            args.duration = 2.0
    elif args.trials:
        trials, title, notice = trials_from_json(args.trials)
    else:
        trials, title = trials_from_cli(args.trial), None
    title = args.title or title or f"Blind {args.mode.upper()} listening test"
    if args.format == "auto":
        audio = "aac" if shutil.which("ffmpeg") else "wav"
    elif args.format == "aac" and not shutil.which("ffmpeg"):
        parser.error("--format aac needs ffmpeg on the PATH")
    else:
        audio = args.format
    if args.abx_repeats < 1:
        parser.error("--abx-repeats must be at least 1")
    build(trials, args.mode, args.seed, args.out, audio, args.rate, args.preroll, args.duration, args.target_lufs,
          args.abx_repeats, title, notice, log=print)
    return 0


if __name__ == "__main__":
    sys.exit(main())
