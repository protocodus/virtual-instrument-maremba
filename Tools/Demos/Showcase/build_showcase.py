#!/usr/bin/env python3
"""Compose and render four original miniatures from the checked-out Maremba DSP.

Requires Python 3, numpy, clang++ and ffmpeg; uses no downloads or sample library.
Run this file from any directory. Existing benchmark/demo files are untouched.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import random
import re
import struct
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[2]


def engine_version():
    # The showcase renders the engine alone, so it carries the engine's X.Y.Z;
    # a Rack release checks that against its own X.Y.Z<stage>N.
    return re.search(r"project\(Maremba VERSION ([0-9.]+)", (PROJECT / "CMakeLists.txt").read_text()).group(1)
RATE = 48000
PPQ = 960
BASE = dict(model=0, malletType=0, malletHardness=.43, strikePosition=.48,
            resonatorTune=0., resonatorCoupling=.72, decay=1.02, buzzAmount=0.,
            artifacts=.08, sympathetic=.24, pitchGlide=0., bodyBloom=.30,
            closeLevel=.85, farLevel=.28 / 3, roomType=2, piezoLevel=.04,
            stereoWidth=1.08, preampDrive=.01, warmth=.05, compAmount=0.,
            compAttack=10., compRelease=120., volume=.80, oversampling=1,
            velocityCurve=1, polyphony=1, tuneCents=0., detune=.5,
            strikeJitter=12., velocityToHardness=.34, velocityToPosition=.18,
            velocityToDecay=.25, velocityToCarving=0., velocityToCoupling=0.,
            velocityToBloom=0., velocityToSympathetic=0., velocityToBuzz=0.,
            velocityToTension=0., velocityToArtifacts=0., tuningScale=0,
            noteOffMode=0, modWheel=0., carving=1., membraneTension=.5)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, **kwargs)


def vlq(value):
    result = [value & 127]
    while value >> 7:
        value >>= 7
        result.insert(0, (value & 127) | 128)
    return bytes(result)


class Piece:
    def __init__(self, slug, title, instrument, bpm, meter, description, seed, params):
        self.slug, self.title, self.instrument = slug, title, instrument
        self.bpm, self.meter, self.description = bpm, meter, description
        self.params = BASE | params
        self.tempo = round(60000000 / bpm)
        self.random = random.Random(seed)
        self.notes = []

    def add(self, beat, pitch, velocity, duration=.28, delay=0.):
        # Timing is quantized to MIDI ticks so the supplied MIDI and rendered
        # performance agree. All phrasing variation is deterministic.
        tick = round((beat + .20 * self.bpm / 60 + delay + self.random.uniform(-.010, .010)) * PPQ)
        velocity = max(1, min(120, round(velocity + self.random.uniform(-2.2, 2.2))))
        self.notes.append((max(0, tick), pitch, velocity, round(duration * PPQ)))

    def chord(self, beat, pitches, velocity, spread=.016):
        for index, pitch in enumerate(pitches):
            self.add(beat, pitch, velocity - index, delay=index * spread)

    def export(self, output):
        events = []
        for tick, pitch, velocity, duration in self.notes:
            events.extend([(tick, 1, pitch, velocity), (tick + duration, 0, pitch, 0)])
        events.sort(key=lambda e: (e[0], e[1]))
        seconds_per_tick = self.tempo / 1e6 / PPQ
        text = [f"# Original Maremba showcase: {self.title} / {self.instrument}",
                f"# {self.bpm} BPM; {self.meter[0]}/{self.meter[1]}; seconds type note velocity",
                "# Timing matches the adjacent MIDI file; note-offs do not damp the engine."]
        text += [f"{tick * seconds_per_tick:.9f} {kind} {note} {velocity}" for tick, kind, note, velocity in events]
        (output / f"{self.slug}.events").write_text("\n".join(text) + "\n")
        name = f"{self.title} — {self.instrument}".encode()
        track = b"\x00\xff\x03" + vlq(len(name)) + name
        track += b"\x00\xff\x51\x03" + self.tempo.to_bytes(3, "big")
        track += b"\x00\xff\x58\x04" + bytes([self.meter[0], int(math.log2(self.meter[1])), 24, 8])
        previous = 0
        for tick, kind, note, velocity in events:
            track += vlq(tick - previous) + bytes([0x90 if kind else 0x80, note, velocity])
            previous = tick
        track += b"\x00\xff\x2f\x00"
        midi = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(track)) + track
        (output / f"{self.slug}.mid").write_bytes(midi)
        (output / f"{self.slug}.parameters.json").write_text(json.dumps(self.params, indent=2) + "\n")
        (output / f"{self.slug}.parameters.txt").write_text("".join(f"{key} {value}\n" for key, value in self.params.items()))
        final_strike = max(t for t, _, _, _ in self.notes) * seconds_per_tick
        duration = round(final_strike + (8.0 if self.params['model'] in (0, 3) else 6.0), 3)
        return events, final_strike, duration


def compose():
    rose = Piece("01_Rosewood_Afterglow", "Afterglow", "Imperial Rosewood", 78, (3, 4),
                 "A luminous waltz. Low open fifths, a singing upper voice, and a final chord left in the hall.",
                 317, {})
    chords = [(38, [57, 61, 64, 66]), (35, [54, 57, 61, 66]), (31, [55, 59, 62, 66]),
              (33, [57, 59, 61, 64]), (31, [55, 59, 62, 66]), (40, [55, 59, 62, 66]),
              (33, [55, 59, 62, 64]), (38, [54, 57, 61, 64])]
    melodies = [[(0, 78), (1.5, 76), (2, 73)], [(0, 74), (1, 73), (2, 69)],
                [(0, 71), (1.5, 74), (2, 78)], [(0, 76), (1, 73), (2.2, 71)],
                [(0, 74), (1, 78), (2, 81)], [(0, 79), (1.5, 78), (2, 76)],
                [(0, 74), (1, 73), (2, 71)], [(0, 73), (1.1, 69), (2.1, 66)]]
    for bar, ((bass, chord), melody) in enumerate(zip(chords, melodies)):
        start = bar * 3 + (0.045 * max(0, bar - 5))
        rose.add(start, bass, 66 if bar < 7 else 59)
        if bar != 7:
            rose.add(start + 1.5, bass + 12, 48)
            for idx, pitch in enumerate(chord):
                rose.add(start + .48 + idx * .5, pitch, 48 + 5 * math.sin(idx), delay=.006 * idx)
        else:
            rose.chord(start + .45, chord, 49, .026)
        for idx, (offset, pitch) in enumerate(melody):
            rose.add(start + offset, pitch, 78 - bar * 1.1 - idx * 4 + (8 if bar == 4 else 0), delay=.025)

    padauk = Piece("02_Padauk_Amber_Steps", "Amber Steps", "Mayan Padauk", 98, (4, 4),
                   "Warm wood and a nimble, syncopated melody. A small-room miniature with an unhurried smile.",
                   521, dict(model=1, malletType=1, malletHardness=.49, decay=.96,
                             roomType=1, farLevel=.27 / 3, piezoLevel=.11, bodyBloom=.35,
                             sympathetic=.17, artifacts=.13, warmth=.08))
    harmony = [(48, [64, 67, 71]), (45, [60, 64, 67]), (50, [60, 64, 65]), (43, [59, 64, 65]),
               (48, [64, 67, 71]), (45, [60, 64, 67]), (43, [59, 62, 65]), (48, [60, 64, 67])]
    melody = [[(0, 76), (.75, 74), (1.5, 72), (2.5, 74), (3, 76)],
              [(0, 79), (1, 76), (1.75, 72), (2.5, 71), (3.5, 72)],
              [(0, 77), (1, 76), (2, 74), (2.75, 72)],
              [(.5, 71), (1.25, 74), (2, 76), (3, 74)],
              [(0, 76), (.75, 79), (1.5, 83), (2.5, 81), (3, 79)],
              [(0, 76), (1, 72), (1.75, 71), (2.5, 69), (3.5, 72)],
              [(0, 74), (1, 76), (1.75, 74), (2.5, 71), (3.5, 67)],
              [(0, 72), (1, 76), (2.25, 72)]]
    for bar, ((bass, notes), tune) in enumerate(zip(harmony, melody)):
        start = bar * 4
        padauk.add(start, bass, 75 if bar < 7 else 64)
        if bar < 7:
            padauk.add(start + 2, bass + 7, 60)
            padauk.chord(start + 1.5, notes[:2], 54)
            padauk.chord(start + 3.25, notes[1:], 47)
        else:
            padauk.chord(start + 2.25, notes, 52)
        for idx, (offset, pitch) in enumerate(tune):
            padauk.add(start + offset, pitch, 83 - idx * 3 - (8 if bar == 7 else 0), delay=.024 if offset % 1 else 0)

    bala = Piece("03_Balafon_Sun_Weave", "Sun Weave", "Balafon Ancestral", 120, (6, 8),
                 "Interlocking pentatonic phrases in six. Soft replies and stronger strokes reveal the wood and gourd resonance.",
                 829, dict(model=2, malletType=2, malletHardness=.52, decay=.95,
                           roomType=0, farLevel=.22 / 3, piezoLevel=.08, bodyBloom=.38,
                           sympathetic=.20, artifacts=.18,
                           strikeJitter=16., warmth=.06, velocityToHardness=.40))
    figures = [[74, 78, 81, 78], [76, 78, 74, 69], [71, 74, 78, 76], [81, 78, 76, 74],
               [74, 76, 78, 81], [83, 81, 78, 76], [78, 74, 71, 69], [76, 78, 76, 74],
               [74, 78, 81, 83], [81, 78, 76, 74], [71, 69, 66, 64], [66, 69, 74]]
    basses = [50, 50, 47, 45, 50, 50, 47, 45, 50, 47, 45, 50]
    for bar, (line, bass) in enumerate(zip(figures, basses)):
        start = bar * 3
        bala.add(start, bass, 84 - (10 if bar == 11 else 0))
        if bar < 11:
            bala.add(start + 1.5, bass + 12, 66)
            bala.add(start + 2.0, 62 if bar % 2 else 69, 44)
        for idx, pitch in enumerate(line):
            offset = [.5, 1., 2., 2.5][idx] if bar < 11 else [.5, 1.2, 2.15][idx]
            bala.add(start + offset, pitch, [79, 60, 89, 66][idx] - (12 if bar == 11 else 0), delay=.012 * (idx % 2))

    kalimba = Piece("04_Kalimba_Firefly_Window", "Firefly Window", "Kalimba Artisan", 84, (4, 4),
                    "A quiet thumb-piano lullaby. Alternating hands trace little lights above a warm, sustained centre.",
                    1031, dict(model=3, malletType=1, malletHardness=.54, strikePosition=.51,
                               decay=.98, roomType=1, farLevel=.25 / 3, piezoLevel=.12,
                               sympathetic=.21, bodyBloom=.29, artifacts=.07,
                               warmth=.02, strikeJitter=11., velocityToHardness=.30,
                               velocityToDecay=.20))
    patterns = [[60, 67, 76, 71, 79, 72, 76, 67], [57, 64, 76, 71, 79, 72, 76, 64],
                [53, 60, 77, 69, 81, 72, 79, 69], [55, 62, 74, 69, 79, 71, 76, 67],
                [60, 67, 76, 71, 83, 79, 76, 72], [57, 64, 81, 72, 79, 76, 72, 64],
                [55, 62, 74, 71, 79, 76, 74, 71], [60, 67, 76, 72, 79]]
    for bar, pattern in enumerate(patterns):
        for idx, pitch in enumerate(pattern):
            beat = bar * 4 + idx * .5
            if bar == 7: beat += idx * .045  # A small written-out rallentando.
            accent = [65, 45, 72, 47, 76, 49, 65, 44][idx]
            kalimba.add(beat, pitch, accent - (9 if bar == 7 else 0), delay=.018 if idx % 2 else 0)
    return [rose, padauk, bala, kalimba]


def loudness(path, raw=False):
    args = ["ffmpeg", "-hide_banner", "-nostats"]
    if raw: args += ["-f", "f32le", "-ar", RATE, "-ac", 2]
    result = run(args + ["-i", path, "-af", "loudnorm=I=-18:TP=-2:LRA=11:print_format=json", "-f", "null", "-"], text=True)
    match = re.search(r'\{\s*"input_i".*?\}', result.stderr, re.S)
    if not match: raise RuntimeError("ffmpeg loudness report absent")
    return {key: float(value) for key, value in json.loads(match.group()).items() if key.startswith('input_')}


def audio_metrics(audio):
    if not np.isfinite(audio).all(): raise RuntimeError("Nonfinite audio")
    peak = float(np.max(np.abs(audio)))
    if peak >= 1.: raise RuntimeError("Clipped audio")
    return dict(sample_peak_dbfs=round(20 * math.log10(max(peak, 1e-12)), 3),
                rms_dbfs=round(20 * math.log10(max(float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))), 1e-12)), 3),
                nonfinite_samples=0, clipped_samples=int(np.count_nonzero(np.abs(audio) >= 1)),
                end_rms_dbfs=round(20 * math.log10(max(float(np.sqrt(np.mean(audio[-RATE//2:].astype(np.float64) ** 2))), 1e-12)), 3),
                final_sample_peak=float(np.max(np.abs(audio[-1]))))


def decode(path):
    return np.frombuffer(run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-acodec", "pcm_f32le", "-"]).stdout, dtype="<f4").reshape(-1, 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compose-only", action="store_true")
    parser.add_argument("--output", type=Path, default=HERE,
                        help="Write generated showcases here; sources remain in Tools/Demos/Showcase")
    args = parser.parse_args()
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    pieces = compose()
    plans = [(piece, *piece.export(output)) for piece in pieces]
    if args.compose_only:
        print(json.dumps([{ "title": p.title, "notes": len(p.notes), "seconds": d } for p, _, _, d in plans], indent=2))
        return
    sources = sorted([*PROJECT.glob("Source/DSP/*.h"), *PROJECT.glob("Source/DSP/*.cpp")])
    before = {str(p.relative_to(PROJECT)): sha(p) for p in sources}
    source_digest = hashlib.sha256(json.dumps(before, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    (output / ".build").mkdir(exist_ok=True)
    binary = output / ".build" / "render_showcase"
    compile_args = ["clang++", "-std=c++17", "-O3", "-I", PROJECT / "Source", HERE / "render_showcase.cpp", *sorted(PROJECT.glob("Source/DSP/*.cpp")), "-o", binary]
    compilation = run(compile_args, text=True)
    compile_log = output / ".build" / "compile.txt"
    compile_log.write_text(compilation.stdout + compilation.stderr)
    # Keep the delivery manifest portable without altering the real compiler
    # invocation or requiring a provenance rewrite after rendering.
    portable_compile_args = []
    for argument in compile_args:
        if isinstance(argument, Path):
            try:
                argument = argument.relative_to(PROJECT)
            except ValueError:
                argument = Path("<OUTPUT>") / argument.relative_to(output)
        portable_compile_args.append(str(argument))
    manifest = dict(format_version=2, generated_utc=datetime.now(timezone.utc).isoformat(),
                    engine_version=engine_version(),
                    repository_head=run(["git", "rev-parse", "HEAD"], cwd=PROJECT, text=True).stdout.strip(),
                    dsp_source_sha256=source_digest, dsp_files=before,
                    adapter_sha256=sha(HERE / "render_showcase.cpp"), composer_sha256=sha(__file__),
                    renderer_binary_sha256=sha(binary),
                    compiler=run(["clang++", "--version"], text=True).stdout.splitlines()[0],
                    compile_command=portable_compile_args,
                    compile_command_scope="Paths are relative to the project root; <OUTPUT> denotes --output when outside the project.",
                    compile_returncode=compilation.returncode,
                    compile_log_sha256=sha(compile_log),
                    sample_rate=RATE, bit_depth=24, oversampling_factor=4, block_size_max=64,
                    performance_origin="Four original Maremba compositions freshly rendered from the current source; no third-party MIDI, samples, or external instruments.",
                    processing="Current engine main stereo output with one shared constant mastering gain across all four pieces; no individual normalization. The loudest piece targets −18 LUFS, constrained by a −2 dBTP ceiling across the complete set. Buzz is 0%, Drive is 1%, and the built-in limiter is disabled. Rendering starts with 6–8 seconds of decay and extends in four-second steps if needed, up to a 35-second track limit. Trimming begins only after the final sample above −90 dBFS, retaining at least 600 ms afterwards and at least 20 seconds total. A final 250 ms cosine endpoint fade is below that threshold. No external EQ, compression, reverb, or limiting. PCM conversion uses triangular dither.",
                    listening_status="Objective audio and codec checks completed; no model-accessible listening tool is installed.", tracks=[])
    with tempfile.TemporaryDirectory(prefix="maremba-showcase-") as temporary:
        temp = Path(temporary)
        renders = []
        for piece, events, last_strike, duration in plans:
            print(f"Rendering {piece.title} ({duration:.2f}s)", flush=True)
            raw = temp / f"{piece.slug}.f32"
            params, event_path = output / f"{piece.slug}.parameters.txt", output / f"{piece.slug}.events"
            command = [binary, params, event_path, raw, duration, RATE]
            metrics = json.loads(run(command, text=True).stdout)
            raw_loudness = loudness(raw, raw=True)
            renders.append([piece, last_strike, duration, raw, params, event_path, metrics, raw_loudness])
        # Physical tails can change between engine revisions. Re-render any
        # insufficient horizon instead of fading out audible sound. Recompute
        # the one shared gain after each extension because gated LUFS may move.
        while True:
            gain_db = min(-18 - max(r[-1]['input_i'] for r in renders),
                          -2.0 - max(r[-1]['input_tp'] for r in renders))
            pending = []
            for render in renders:
                samples = np.fromfile(render[3], dtype="<f4").reshape(-1, 2)
                if not np.isfinite(samples).all():
                    raise RuntimeError("Nonfinite raw output")
                audible = np.flatnonzero(np.max(np.abs(samples), axis=1) * 10 ** (gain_db / 20) > 10 ** (-90 / 20))
                if not len(audible):
                    raise RuntimeError(f"Silent showcase: {render[0].title}")
                if (len(samples) - 1 - int(audible[-1])) / RATE < .6:
                    pending.append(render)
            if not pending:
                break
            for render in pending:
                duration = round(render[2] + 4., 3)
                if duration > 35.:
                    raise RuntimeError(f"{render[0].title} needs more than35 seconds to retain its natural tail")
                print(f"Extending {render[0].title} to {duration:.2f}s to retain its natural tail", flush=True)
                command = [binary, render[4], render[5], render[3], duration, RATE]
                render[2] = duration
                render[6] = json.loads(run(command, text=True).stdout)
                render[7] = loudness(render[3], raw=True)
        manifest['common_gain_db'] = round(gain_db, 5)
        print(f"Shared mastering gain: {gain_db:+.5f} dB", flush=True)
        for piece, last_strike, duration, raw, params, event_path, metrics, raw_loudness in renders:
            samples = np.fromfile(raw, dtype="<f4").reshape(-1, 2)
            if not np.isfinite(samples).all(): raise RuntimeError("Nonfinite raw output")
            samples *= 10 ** (gain_db / 20)
            last_tail_frame = int(np.flatnonzero(np.max(np.abs(samples), axis=1) > 10**(-90/20))[-1])
            duration = min(duration, max(20., math.ceil((last_tail_frame / RATE + .6) * 10) / 10))
            samples = samples[:round(duration * RATE)]
            fade_frames = RATE // 4
            samples[-fade_frames:] *= (.5 + .5 * np.cos(np.linspace(0, math.pi, fade_frames)))[:, None]
            samples.tofile(raw)
            wav, aac = output / f"{piece.slug}.wav", output / f"{piece.slug}.m4a"
            run(["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ar", RATE, "-ac", 2, "-i", raw,
                 "-af", "aresample=dither_method=triangular", "-c:a", "pcm_s24le", wav])
            run(["ffmpeg", "-v", "error", "-y", "-i", wav, "-c:a", "aac", "-b:a", "192k",
                 "-metadata", f"title={piece.title}", "-metadata", "artist=Protocodus · Maremba",
                 "-metadata", f"comment=Original {piece.instrument} showcase; current physical-model engine",
                 "-movflags", "+faststart", aac])
            pcm, compressed = decode(wav), decode(aac)
            wav_metrics, aac_metrics = audio_metrics(pcm), audio_metrics(compressed)
            wav_loudness, aac_loudness = loudness(wav), loudness(aac)
            if wav_loudness['input_tp'] > -1.5 or aac_loudness['input_tp'] > -.5:
                raise RuntimeError("Insufficient reconstructed peak headroom")
            if len(pcm) != round(duration * RATE): raise RuntimeError("Incorrect WAV duration")
            if wav_metrics['end_rms_dbfs'] > -60: raise RuntimeError("Tail ends too early")
            waveform = [round(float(np.max(np.abs(part))), 5) for part in np.array_split(pcm, 128)]
            paths = [wav, aac, event_path, output / f"{piece.slug}.mid", params, output / f"{piece.slug}.parameters.json"]
            track = dict(slug=piece.slug, title=piece.title, instrument=piece.instrument,
                         description=piece.description, model=piece.params['model'], bpm=piece.bpm,
                         meter=f"{piece.meter[0]}/{piece.meter[1]}", duration_seconds=duration,
                         delivered_frames=len(pcm), final_sample_above_minus90_seconds=round(last_tail_frame/RATE, 6),
                         note_count=len(piece.notes), last_strike_seconds=round(last_strike, 6),
                         natural_tail_seconds=round(duration-last_strike, 4),
                         velocity_range=[min(n[2] for n in piece.notes), max(n[2] for n in piece.notes)],
                         midi_note_range=[min(n[1] for n in piece.notes), max(n[1] for n in piece.notes)],
                         parameters=piece.params, constant_gain_db=round(gain_db, 5),
                         raw_engine=metrics, raw_loudness=raw_loudness, wav_metrics=wav_metrics,
                         aac_metrics=aac_metrics, wav_loudness=wav_loudness, aac_loudness=aac_loudness,
                         files={p.name: dict(bytes=p.stat().st_size, sha256=sha(p)) for p in paths}, waveform=waveform)
            manifest['tracks'].append(track)
            print(f"  WAV {wav_loudness['input_i']:.1f} LUFS / {wav_loudness['input_tp']:.1f} dBTP; AAC {aac_loudness['input_tp']:.1f} dBTP; tail {wav_metrics['end_rms_dbfs']:.1f} dBFS", flush=True)
    after = {str(p.relative_to(PROJECT)): sha(p) for p in sources}
    if before != after: raise RuntimeError("DSP changed during rendering; regenerate against a stable revision")
    (output / "SHOWCASE_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    html_tracks = []
    for track in manifest['tracks']:
        html_tracks.append(track | dict(audio="data:audio/mp4;base64," + base64.b64encode((output / (track['slug'] + '.m4a')).read_bytes()).decode()))
    template = (HERE / "player_template.html").read_text()
    (output / "Maremba_Showcase.html").write_text(template.replace("/*TRACKS*/[]", json.dumps(html_tracks)).replace("__VERSION__", manifest['engine_version']).replace("__DSP_SHA__", source_digest))
    rows = ["| Instrument / piece | Length | Notes / velocity | WAV LUFS / dBTP | AAC dBTP | Final 0.5 s RMS |",
            "|---|---:|---|---|---:|---:|"]
    for t in manifest['tracks']:
        rows.append(f"| {t['instrument']} — {t['title']} | {t['duration_seconds']:.2f} s | {t['note_count']} / {t['velocity_range'][0]}–{t['velocity_range'][1]} | {t['wav_loudness']['input_i']:.1f} / {t['wav_loudness']['input_tp']:.1f} | {t['aac_loudness']['input_tp']:.1f} | {t['wav_metrics']['end_rms_dbfs']:.1f} dBFS |")
    report = f"""# Maremba — four small worlds

Original release showcases for **{manifest['engine_version']}**. Open `Maremba_Showcase.html`: all four AAC previews are embedded, so it works offline as a single file. The adjacent WAVs are the 48 kHz, stereo, 24-bit delivery masters. AAC previews use 192 kbit/s.

""" + "\n".join(rows) + f"""

Each piece has its own harmonies, register, phrase shape, deterministic timing and velocity variation. The engine supplies contact and overtone variation. These are short original compositions, not adaptations of third-party repertoire. No samples or external instruments were used.

The supplied `.mid` files and `.events` contain the same performance timing (960 ticks per quarter, with the exported tempo); all parameters are explicit in `.parameters.json` and `.parameters.txt`. Note-offs are retained for normal MIDI use, although Maremba lets its bars and tines ring freely. Rendering initially reserves 6–8 seconds for decay and extends in four-second steps when needed, up to a 35-second track limit. Delivery silence is trimmed only after the last sample above −90 dBFS, retaining at least another 600 ms and a minimum track length of 20 seconds. The 250 ms endpoint fade starts after that threshold. All four pieces use the same **{gain_db:+.5f} dB** constant gain: the loudest piece targets −18 LUFS, constrained by a −2 dBTP ceiling across the complete set. No individual normalization is applied, preserving the engine's relative instrument levels and dynamics. Buzz is 0%, Drive is 1%, and the built-in limiter and external processing are disabled. The built-in room differs by instrument.

The renderer compiles the actual checked-out DSP files, runs at 48 kHz with 4× internal oversampling and at most 64 frames per call, and splits calls at every exact note frame. The manifest hashes the complete DSP source set, adapter, compiler output, MIDI, events, parameters and audio. It also records the checked-out Git HEAD; the DSP hashes are authoritative if the worktree contains uncommitted source changes.

DSP set SHA-256: `{source_digest}`

Repository HEAD at rendering: `{manifest['repository_head']}`

Validation: every raw engine sample and decoded PCM/AAC sample is finite. Decoded previews and delivery masters have no clipped samples; true peaks and full decay tails were measured with ffmpeg. The DSP source set was unchanged before/after rendering. Objective checks are recorded; no model-accessible listening tool was available, so subjective audition is not claimed.

Rebuild from the current checkout:

```sh
python3 Tools/Demos/Showcase/build_showcase.py --output Release/validation/{manifest['engine_version']}/media/Showcase
python3 Tools/Demos/Showcase/validate_showcase.py --input Release/validation/{manifest['engine_version']}/media/Showcase
```

Requires Python 3 with NumPy, clang++ and ffmpeg. `--compose-only` regenerates the original MIDI/events/parameter files without rendering. `--output` keeps previous delivered audio untouched. Existing demos outside that output folder are preserved.
"""
    (output / "README.md").write_text(report)
    print(f"Created {output / 'Maremba_Showcase.html'}", flush=True)


if __name__ == "__main__":
    main()
