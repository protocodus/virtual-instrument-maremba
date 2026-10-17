#!/usr/bin/env python3
"""Regenerate all Maremba WAV/AAC demos, measured report and embedded player.

Run from any directory with Python 3, clang++ and ffmpeg. Cached MIDI inputs
are SHA-256 checked; no network is used unless --download-missing is passed.
Audio/ is intentionally untracked. The compact, self-contained Docs/audio/demo-player.html
embeds 12 repertoire comparisons and all original acoustic and idiom studies.
Third-party sequence adaptations retain the licences in the root THIRD_PARTY_NOTICES.md.
"""
import argparse
import array
import base64
import hashlib
import json
import math
import platform
import shutil
import struct
import subprocess
import sys
import urllib.request
import wave
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
PROJECT_DIR = BASE_DIR.parents[1]
DSP_DIR = PROJECT_DIR / "Source" / "DSP"
MIDI_DIR = BASE_DIR / "Midi"
EVENTS_DIR = BASE_DIR / "Events"
AUDIO_DIR = BASE_DIR / "Audio"
RENDERER_BIN = BASE_DIR / "maremba_renderer"
# Committed outputs live in the Docs/ taxonomy; Audio/ holds everything else.
PLAYER = PROJECT_DIR / "Docs" / "audio" / "demo-player.html"
MANIFEST = PROJECT_DIR / "Docs" / "audio" / "demo-player-manifest.json"
REPORT = PROJECT_DIR / "Docs" / "benchmarks" / "demo-renders.md"
REPEATED_STUDY_KEY = "09_Repeated_Hits"
IDIOM_CATEGORY = "Idioms"

SONGS = {
    "01_Bach_Prelude_C_Major": {
        "title": "J.S. Bach - Prelude in C Major, BWV 846",
        "url": "https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/bach_846.mid",
        "filename": "01_Bach_Prelude_C_Major.mid",
        "credit": "Bernd Krueger, piano-midi.de (CC BY-SA 3.0 DE)"
    },
    "02_Beethoven_Fur_Elise": {
        "title": "L. v. Beethoven - Für Elise (Poco moto)",
        "url": "https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/for_elise_by_beethoven.mid",
        "filename": "02_Beethoven_Fur_Elise.mid",
        "credit": "Bernd Krueger, piano-midi.de (CC BY-SA 3.0 DE)"
    },
    "03_Joplin_The_Entertainer": {
        "title": "Scott Joplin - The Entertainer (Ragtime Two-Step)",
        "url": "https://raw.githubusercontent.com/ioccc-src/winner/master/2020/otterness/entertainer.mid",
        "filename": "03_Joplin_The_Entertainer.mid",
        "credit": "IOCCC 2020 'otterness' entry (CC BY-SA 4.0)"
    },
    "04_Beethoven_Moonlight_Sonata": {
        "title": "L. v. Beethoven - Moonlight Sonata (Adagio sostenuto)",
        "url": "https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/mond_1.mid",
        "filename": "04_Beethoven_Moonlight_Sonata.mid",
        "credit": "Bernd Krueger, piano-midi.de (CC BY-SA 3.0 DE)"
    },
    "05_Mozart_Sonata_Facile": {
        "title": "W.A. Mozart - Piano Sonata No. 16 in C Major, K. 545",
        "url": "https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/mz_545_1.mid",
        "filename": "05_Mozart_Sonata_Facile.mid",
        "credit": "Bernd Krueger, piano-midi.de (CC BY-SA 3.0 DE)"
    }
}

MODELS = [
    {"id": 0, "code": "Rosewood", "name": "Imperial Rosewood 5.0 (Concert Grand)", "default_mallet": 0},
    {"id": 1, "code": "Padauk",   "name": "Mayan Padauk 4.3 (Artisan Wood Bark)", "default_mallet": 1},
    {"id": 2, "code": "Balafon",  "name": "Balafon Ancestral (Calabash Resonance)", "default_mallet": 2},
    {"id": 3, "code": "Kalimba",  "name": "Kalimba Artisan 17-Key (African Thumb Piano)", "default_mallet": 1}
]

STRIKER_COMPARISONS = [
    {
        "song_key": "01_Bach_Prelude_C_Major",
        "model_id": 0,
        "model_code": "Rosewood",
        "mallets": [
            (0, "SoftYarn", "Soft Wool Yarn"),
            (1, "MediumCord", "Medium Concert Cord"),
            (2, "HardRubber", "Hard Rubber"),
            (3, "WoodBaton", "Wood Baton")
        ]
    },
    {
        "song_key": "04_Beethoven_Moonlight_Sonata",
        "model_id": 3,
        "model_code": "Kalimba",
        "mallets": [
            (0, "ThumbFlesh", "Thumb Flesh Pad"),
            (1, "NaturalThumb", "Natural Thumb"),
            (2, "ThumbnailSnap", "Thumbnail Snap"),
            (3, "ThumbPick", "Thumb Pick")
        ]
    }
]


MIDI_HASHES = {
    "01_Bach_Prelude_C_Major": "107f2b6a298f283f8d26d4e8359669e7780f7ea64011b7844f2ed066d874a37e",
    "02_Beethoven_Fur_Elise": "a1481bdafe94d33e0de8934c97aec45534c908bc797bae7293f626074aec2e07",
    "03_Joplin_The_Entertainer": "780e68865a83a6649f9e7f5d002cd3b8700213a1fde32a9cac38c9242acf5f83",
    "04_Beethoven_Moonlight_Sonata": "074a76e133bedf8005ff1e586beec9d4e97c013bcc72d56ff6df4e3c0d4e7c01",
    "05_Mozart_Sonata_Facile": "010bd27e25904822e04a51f207083722e469fccb0b446ce5545fbd37378268bf",
}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def ensure_midi_files(download=False):
    MIDI_DIR.mkdir(parents=True, exist_ok=True)
    for key, info in SONGS.items():
        dest = MIDI_DIR / info["filename"]
        if not dest.exists():
            if not download:
                raise RuntimeError(f"Missing cached {dest.name}; use --download-missing to restore it")
            with urllib.request.urlopen(info["url"], timeout=20) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != MIDI_HASHES[key]:
                raise RuntimeError(f"Upstream bytes changed for {dest.name}")
            dest.write_bytes(data)
        if sha256(dest) != MIDI_HASHES[key]:
            raise RuntimeError(f"MIDI provenance check failed: {dest.name}")


def parse_smf(data):
    """Read checked SMF 0/1 with tempo maps, running status and length-coded SysEx.

    Sustain/controllers are deliberately omitted: these acoustic bars ring freely.
    Unsupported SMPTE timing/type 2, malformed chunks and truncated events fail.
    """
    if len(data) < 14:
        raise ValueError("Truncated MIDI header")
    magic, length, fmt, count, division = struct.unpack(">4sIHHH", data[:14])
    if magic != b"MThd" or length < 6 or fmt not in (0, 1) or not count:
        raise ValueError("Expected SMF type 0 or 1")
    if not division or division & 0x8000:
        raise ValueError("Expected MIDI ticks-per-quarter timing")
    pos, merged = 8 + length, []
    for _ in range(count):
        if pos + 8 > len(data):
            raise ValueError("Truncated MIDI track header")
        magic, size = struct.unpack(">4sI", data[pos:pos + 8])
        pos += 8
        if magic != b"MTrk" or pos + size > len(data):
            raise ValueError("Invalid or truncated MIDI track")
        track = data[pos:pos + size]
        pos += size
        ptr, tick, running = 0, 0, None

        def take(n):
            nonlocal ptr
            if ptr + n > len(track):
                raise ValueError("Truncated MIDI event")
            value = track[ptr:ptr + n]
            ptr += n
            return value

        def vlq():
            value = 0
            for _ in range(4):
                b = take(1)[0]
                value = (value << 7) | (b & 127)
                if not b & 128:
                    return value
            raise ValueError("MIDI VLQ exceeds four bytes")

        while ptr < len(track):
            tick += vlq()
            if ptr >= len(track):
                raise ValueError("Missing MIDI event status")
            status = track[ptr]
            if status & 128:
                ptr += 1
            elif running is not None:
                status = running
            else:
                raise ValueError("Running status without a channel status")
            if status == 0xFF:
                running = None
                kind = take(1)[0]
                payload = take(vlq())
                if kind == 0x51:
                    if len(payload) != 3 or not int.from_bytes(payload, "big"):
                        raise ValueError("Invalid tempo event")
                    merged.append((tick, "tempo", int.from_bytes(payload, "big"), 0))
                if kind == 0x2F:
                    break
            elif status in (0xF0, 0xF7):
                running = None
                take(vlq())  # SMF SysEx has a VLQ length, not a sentinel terminator.
            elif 0x80 <= status <= 0xEF:
                running = status
                kind = status & 0xF0
                payload = take(1 if kind in (0xC0, 0xD0) else 2)
                if any(b & 128 for b in payload):
                    raise ValueError("Invalid channel-event data byte")
                if kind in (0x80, 0x90):
                    note, velocity = payload
                    on = kind == 0x90 and velocity > 0
                    merged.append((tick, "on" if on else "off", note, velocity if on else 0))
            else:
                raise ValueError("Unsupported MIDI status")
    merged.sort(key=lambda item: item[0])  # Preserve same-frame event order.
    seconds, previous, tempo, timed = 0.0, 0, 500000, []
    for tick, kind, a, b in merged:
        seconds += (tick - previous) * tempo / (division * 1000000.0)
        previous = tick
        if kind == "tempo":
            tempo = a
        else:
            timed.append((seconds, int(kind == "on"), a, b))
    return timed


def repeated_hit_events():
    """Original fixed-input listening study; the engine supplies the variation."""
    lines = ["# Protocodus original study: repeated C4, fixed MIDI velocity 90",
             "# Twelve identical note-ons, 500 ms apart; Variance=0, Artifacts=0.",
             "# No parameter automation or per-hit level normalization."]
    lines += [f"{index * 0.5:.9f} 1 60 90" for index in range(12)]
    return "\n".join(lines) + "\n"


def _rows(notes):
    """(time, note, velocity, length) tuples -> sorted note-on/off event rows."""
    rows = []
    for time, note, velocity, length in notes:
        rows.append((time, 1, note, velocity))
        rows.append((time + length, 0, note, 0))
    return rows


def _wheel(time, value):
    return (time, 2, 1, int(round(min(max(value, 0), 127))))


def _render_rows(header, rows):
    rows = sorted(rows, key=lambda row: (row[0], row[1] == 1))  # offs and wheel before ons
    return "\n".join([*header, "# Format: time_sec type(1:on,0:off,2:controller 1 = mod wheel) note|cc velocity|value",
                       *(f"{t:.9f} {kind} {a} {b}" for t, kind, a, b in rows)]) + "\n"


def idiom_events():
    """Original Protocodus phrases for the idiom studies (no third-party material)."""
    studies = {}

    # Rolled chorale: four held keys per chord, the mod wheel sets the roll speed.
    chords = [(0.0, (48, 55, 64, 72), 56), (2.0, (45, 57, 64, 72), 64), (4.0, (41, 57, 65, 72), 72),
              (6.0, (43, 59, 65, 74), 80), (8.0, (36, 55, 64, 72), 64)]
    rows = [_wheel(0.0, 70), _wheel(6.0, 80), _wheel(8.0, 64), _wheel(10.5, 0)]
    for start, keys, velocity in chords:
        rows += _rows((start, key, velocity, 3.0 if start == 8.0 else 1.98) for key in keys)
    studies["10_Roll_Chorale"] = _render_rows(
        ["# Protocodus original study: rolled chorale; the mod wheel rolls the held keys",
         "# (70/127, 80/127 at the climax); the wheel returns to 0 and the last chord rings."], rows)

    # Guatemalan-style melody in thirds: struck eighths, redoble (rolled) long notes.
    upper = [(71, 72, 74, 76), (74, 72, 71, 69), (71, 72, 74, 79), (78, 76, 74)]
    lower = [(67, 69, 71, 72), (71, 69, 67, 66), (67, 69, 71, 76), (74, 72, 71)]
    rows = []
    for bar, (top, bottom) in enumerate(zip(upper, lower)):
        start = 1.2 * bar
        for index, (a, b) in enumerate(zip(top, bottom)):
            t = start + 0.2 * index
            if index < 3:
                rows += _rows([(t, a, 84 if index == 0 else 74, 0.18), (t, b, 76 if index == 0 else 68, 0.18)])
            else:
                rows += _rows([(t, a, 78, 0.58), (t, b, 70, 0.58)])
                rows += [_wheel(t, 76), _wheel(t + 0.58, 0)]
    rows += _rows((4.2, key, 74, 1.8) for key in (55, 67, 71, 74, 79))
    rows += [_wheel(4.2, 76), _wheel(6.0, 0)]
    studies["11_Guatemalan_Redoble"] = _render_rows(
        ["# Protocodus original study: melody in parallel thirds, 6/8; long notes and the",
         "# final chord are rolled (redoble) by raising the mod wheel while the keys are held."], rows)

    # Bala ostinato on the white keys (7-equal): low anchor and a melodic answer.
    pulse, rows = 0.16, []
    low = {0: 48, 3: 55, 6: 57, 9: 55}
    high = {1: 64, 2: 62, 4: 64, 5: 67, 7: 69, 8: 67, 10: 64, 11: 62}
    for cycle in range(5):
        for step in range(12):
            t = (12 * cycle + step) * pulse
            if step in low:
                rows += _rows([(t, low[step], 90 if step == 0 else 80, 0.15)])
            if step in high:
                note = high[step] + (12 if cycle in (2, 3) and step >= 7 else 0)
                rows += _rows([(t, note, 84 if step in (4, 10) else 70, 0.15)])
    rows += _rows([(60 * pulse, 48, 96, 0.3), (60 * pulse, 60, 84, 0.3)])
    studies["12_Bala_Ostinato"] = _render_rows(
        ["# Protocodus original study: a 12-pulse bala ostinato on the white keys, where the",
         "# Bala 7-equal tuning places the heptatonic scale; accents on pulses 0, 4 and 10."], rows)

    # Amadinda-style interlocking: two parts on alternate pulses, each in octaves.
    pulse, rows = 0.11, []
    first = [72, 69, 67, 69, 64, 62, 64, 67, 69, 72, 74, 72]
    second = [64, 62, 60, 62, 67, 64, 62, 60, 62, 64, 67, 69]
    for cycle in range(4):
        for index in range(12):
            t = (24 * cycle + 2 * index) * pulse
            for part, t0, velocity in ((first, t, 78), (second, t + pulse, 70)):
                accent = 8 if index % 3 == 0 else 0
                rows += _rows([(t0, part[index], velocity + accent, 0.1), (t0, part[index] - 12, velocity - 6 + accent, 0.1)])
    rows += _rows([(96 * pulse, 60, 90, 0.3), (96 * pulse, 48, 84, 0.3)])
    studies["13_Amadinda_Interlock"] = _render_rows(
        ["# Protocodus original study: two interlocking 12-note parts on alternate pulses, each",
         "# doubled at the octave, on the C D E G A keys of the 5-equal pentatonic tuning."], rows)

    # Mbira-style kalimba pattern; the second half moves a hand over the sound holes.
    pulse, rows = 0.18, []
    pattern = [60, 72, 67, 76, 64, 74, 67, 79]
    for cycle in range(8):
        for index, note in enumerate(pattern):
            t = (8 * cycle + index) * pulse
            rows += _rows([(t, note, 82 if index % 4 == 0 else 70, 0.17)])
    wah_start, wah_end = 32 * pulse, 64 * pulse
    t = wah_start
    while t < wah_end:
        rows.append(_wheel(t, 63.5 * (1 - math.cos(2 * math.pi * 3.0 * (t - wah_start)))))
        t = round(t + 0.02, 9)
    rows.append(_wheel(wah_end, 0))
    rows += _rows((wah_end, note, 80, 0.5) for note in (60, 64, 67))
    studies["14_Kalimba_Hole_Wah"] = _render_rows(
        ["# Protocodus original study: an interlocking thumb pattern, first with open sound holes,",
         "# then with a hand opening and closing the holes three times a second (mod wheel)."], rows)

    # Note-off Damp: the same phrase legato, then staccato; the hand stops each key.
    phrase = [60, 64, 67, 72, 71, 67, 64, 62]
    rows = []
    for index, note in enumerate(phrase):
        rows += _rows([(0.25 * index, note, 80, 0.24), (2.2 + 0.25 * index, note, 80, 0.08)])
    rows += _rows((4.4, note, 76, 2.0) for note in (48, 60, 64, 67))
    studies["15_Dampened_Etude"] = _render_rows(
        ["# Protocodus original study: a phrase held legato, then staccato, then a chord released",
         "# after two seconds; with Note-off Damp each released key is stopped by the hand."], rows)
    return studies


IDIOM_STUDIES = [
    # key, title, model id, striker, duration (s: the last event plus the renderer's
    # three-second tail, so every note-off and wheel event is played), options
    ("10_Roll_Chorale", "Rolled chorale · mod-wheel roll", 0, 0, 15, ()),
    ("11_Guatemalan_Redoble", "Melody in thirds · redoble rolls", 1, 1, 10, ()),
    ("12_Bala_Ostinato", "Bala ostinato · Bala 7-equal tuning", 2, 2, 13, ("--scale", "1")),
    ("13_Amadinda_Interlock", "Interlocking parts · 5-equal pentatonic", 2, 3, 14, ("--scale", "3")),
    ("14_Kalimba_Hole_Wah", "Thumb pattern · hole-cover wah", 3, 1, 16, ()),
    ("15_Dampened_Etude", "Legato and staccato · Note-off Damp", 0, 1, 10, ("--note-off", "1")),
]


def convert_midi_to_events():
    EVENTS_DIR.mkdir(parents=True, exist_ok=True)
    for key, info in SONGS.items():
        events = parse_smf((MIDI_DIR / info["filename"]).read_bytes())
        lines = [f"# Maremba Event Stream: {info['title']}",
                 "# Format: time_sec event_type(1:on,0:off) note velocity"]
        lines += [f"{t:.9f} {kind} {note} {vel}" for t, kind, note, vel in events]
        (EVENTS_DIR / f"{key}.events").write_text("\n".join(lines) + "\n")
    # Original studies are Protocodus-authored, without third-party MIDI material.
    (EVENTS_DIR / "06_Position_Study.events").write_text(
        "# Protocodus original study: identical C4 strikes at velocity 90\n"
        "0.000000000 1 60 90\n1.100000000 1 60 90\n2.200000000 1 60 90\n")
    (EVENTS_DIR / "07_Dynamics_Study.events").write_text(
        "# Protocodus original study: C4 pp-to-ff velocities 16, 32, 64, 96, 127\n"
        "0.000000000 1 60 16\n1.100000000 1 60 32\n2.200000000 1 60 64\n3.300000000 1 60 96\n4.400000000 1 60 127\n")
    (EVENTS_DIR / "08_Pluck_Study.events").write_text(
        "# Protocodus original study: identical C4 plucks at velocity 90\n"
        "0.000000000 1 60 90\n1.100000000 1 60 90\n2.200000000 1 60 90\n")
    (EVENTS_DIR / f"{REPEATED_STUDY_KEY}.events").write_text(repeated_hit_events())
    for key, text in idiom_events().items():
        (EVENTS_DIR / f"{key}.events").write_text(text)


def compile_renderer():
    sources = sorted(DSP_DIR.glob("*.cpp"))
    deps = [BASE_DIR / "maremba_renderer.cpp", *DSP_DIR.glob("*.h"), *sources]
    if not RENDERER_BIN.exists() or RENDERER_BIN.stat().st_mtime < max(p.stat().st_mtime for p in deps):
        subprocess.run(["clang++", "-std=c++17", "-O3", "-I", str(DSP_DIR.parent),
                        *(str(p) for p in sources), str(BASE_DIR / "maremba_renderer.cpp"),
                        "-o", str(RENDERER_BIN)], check=True)


def verify_wav(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (2, 2, 44100):
            raise RuntimeError(f"Unexpected WAV format: {path}")
        samples = array.array("h", source.readframes(source.getnframes()))
        if sys.byteorder != "little":
            samples.byteswap()
    peak = max(abs(value) for value in samples)
    if not peak or peak >= 32767:
        raise RuntimeError(f"Silent or clipped WAV: {path}")
    return {"pcm_peak": peak / 32768, "pcm_clipped_samples": sum(abs(v) >= 32767 for v in samples)}


def encode_aac(wav):
    output = wav.with_suffix(".m4a")
    # No temporal noise shaping, intensity stereo or noise substitution: on
    # hard-mallet attacks and loud low chords these tools made FFmpeg 8's
    # native encoder overshoot the PCM peak by up to 8 dB; without them the
    # decoded peak stays with the PCM's.
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                    "-map_metadata", "-1", "-c:a", "aac", "-b:a", "192k",
                    "-aac_tns", "0", "-aac_is", "0", "-aac_pns", "0",
                    "-movflags", "+faststart", str(output)], check=True)
    # Decode the actual delivery codec; an unclipped WAV alone does not prove AAC headroom.
    raw = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(output),
                          "-f", "f32le", "-acodec", "pcm_f32le", "-"],
                         check=True, capture_output=True).stdout
    samples = array.array("f", raw)
    if sys.byteorder != "little":
        samples.byteswap()
    if not samples or any(not math.isfinite(v) for v in samples):
        raise RuntimeError(f"Non-finite or empty AAC: {output}")
    peak = max(abs(v) for v in samples)
    if peak >= 1:
        raise RuntimeError(f"AAC reconstruction clips ({peak}): {output}")
    return output, {"aac_decoded_peak": peak, "aac_decoded_clipped_samples": 0}


def render_one(key, title, model, mallet, tag, category, duration, oversampling, position=None, options=()):
    wav = AUDIO_DIR / f"{key}_{model['code']}{tag}.wav"
    cmd = [str(RENDERER_BIN), "--events", str(EVENTS_DIR / f"{key}.events"),
           "--output", str(wav), "--model", str(model["id"]), "--mallet", str(mallet),
           "--oversampling", str(oversampling), "--max-duration", str(duration), "--json", *options]
    if position is not None:
        cmd += ["--strike-position", str(position), "--controlled-strike"]
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    metrics = json.loads(result.stdout)
    metrics.update(verify_wav(wav))
    aac, codec_metrics = encode_aac(wav)
    metrics.update(codec_metrics)
    metrics.update(song_key=key, song_title=title, category=category, wav=wav.name, aac=aac.name,
                   wav_sha256=sha256(wav), aac_sha256=sha256(aac))
    metrics.pop("output_file")
    metrics["credit"] = SONGS[key]["credit"] if key in SONGS else "Original Protocodus acoustic study"
    metrics["license"] = "CC BY-SA 4.0" if key.startswith("03_") else "CC BY-SA 3.0 DE" if key in SONGS else "Project licence"
    print(f"{wav.stem}: {metrics['audio_duration_sec']:.1f}s, {metrics['real_time_factor']:.1f}x, "
          f"{metrics['peak_db']:.1f} dBFS", flush=True)
    return metrics


def generate_players(results, provenance):
    template = (BASE_DIR / "player_template.html").read_text()
    # Keep every previously shipped embedded comparison. Add original studies;
    # the rest of the 5 × 4 repertoire set remains in the full local player.
    selected = [r for r in results if r["category"] != "Showcase" or r["song_key"].startswith("01_")]
    for destination, tracks, embedded in [(PLAYER, selected, True),
                                          (AUDIO_DIR / "demo_player.html", results, False)]:
        payload = []
        for r in tracks:
            track = dict(r)
            track["audio"] = ("data:audio/mp4;base64," + base64.b64encode((AUDIO_DIR / r["aac"]).read_bytes()).decode()
                              if embedded else r["aac"])
            payload.append(track)
        html = template.replace("/*__TRACKS__*/[]", json.dumps(payload, ensure_ascii=False).replace("</", "<\\/"))
        html = html.replace("__SOURCE_DIGEST__", provenance["dsp_sha256"][:16])
        html = html.replace("__NOTICES_LINK__", "../../THIRD_PARTY_NOTICES.md" if embedded else "../../../THIRD_PARTY_NOTICES.md")
        destination.write_text(html)
    return selected


def write_report(results, manifest):
    audio_seconds = sum(r["audio_duration_sec"] for r in results)
    compute_seconds = sum(r["render_time_ms"] for r in results) / 1000
    lines = ["# Maremba demo rendering and audio verification", "",
             f"- {len(results)} stereo 44.1 kHz / 16-bit WAVs and 192 kbit/s AAC files regenerated from current DSP.",
             f"- {audio_seconds:.1f} seconds of audio; {compute_seconds:.2f} seconds of native engine rendering ({audio_seconds / compute_seconds:.1f}× aggregate real-time).",
             "- All source samples finite; all WAVs non-silent with zero clipped PCM samples; all decoded AACs finite with peaks below 0 dBFS.",
             f"- Maximum decoded AAC peak: {max(20 * math.log10(r['aac_decoded_peak']) for r in results):.2f} dBFS.",
             "- 64-frame host blocks split at exact event frames, 24 voices; oversampling: " + ", ".join(sorted({r["oversampling"] for r in results})) + ".",
             "- Repertoire excerpts reserve the final three seconds for acoustic decay; a 10 ms endpoint fade prevents truncated tails clicking.",
             "- No loudness normalization or mastering compression. Safety attenuation, if needed, is disclosed per render in the manifest.",
             "- Every demo preset uses Buzz 0% and Drive 1%; actual values are recorded per render in the manifest.",
             f"- {sum(r['category'] == IDIOM_CATEGORY for r in results)} original idiom studies play the engine's performance features: mod-wheel rolls, "
             "the Kalimba's hole cover, the Bala 7-equal and 5-equal pentatonic tunings and Note-off Damp (tuning, note-off mode and wheel events are recorded per render).",
             "", "These are native engine throughput measurements, including parameter updates and event scheduling. "
             "They exclude encoding, file IO, the Rack wrapper, Reason scheduling and target-chip performance; they are not a host CPU/deadline guarantee.", "",
             f"Machine: `{manifest['machine']}`. Compiler: `{manifest['compiler']}`.",
             f"DSP source digest: `{manifest['dsp_sha256']}`.", "",
             "Reproduce with `python3 Tools/Demos/run_benchmark.py`. WAV/AAC files and the full local player are written into ignored `Tools/Demos/Audio/`. "
             "The committed [demo player](../audio/demo-player.html) is self-contained and rebuilt by the same command. "
             "[Its manifest](../audio/demo-player-manifest.json) records every input and output SHA-256, codec peak and raw renderer metric.", "",
             "The twelve previously shipped repertoire comparisons are embedded, together with twenty-eight original touch studies and six idiom studies. "
             "Original Touch studies set Variance and Artifacts to zero; subtle intrinsic variation remains. Each comparison reuses its fixed input event sequence. Bar position comparisons hold MIDI note, velocity and gain constant at centre (50%), off-centre (35%), "
             "the fundamental node (22.42%) and edge (10%). Kalimba comparisons use control positions 10%, 35%, 50% and 90% along the playable end (physical tine range 65–100%). "
             "Dynamics studies play MIDI velocities 16, 32, 64, 96 and 127 at fixed gain; four same-note Kalimba studies isolate the thumb/pluck choice. Four repeated-hit studies each play twelve C4 notes at MIDI velocity 90, spaced 500 ms apart, followed by three seconds of decay. Their controls remain fixed and no per-note normalization is applied. "
             "The idiom studies are original phrases: a rolled chorale (Rosewood, soft yarn), a melody in thirds with rolled long notes (Padauk), "
             "a bala ostinato in the Bala 7-equal tuning and interlocking parts in the 5-equal pentatonic tuning (Balafon), a thumb pattern with the "
             "Kalimba's sound holes opened and closed three times a second, and a phrase played legato and staccato with Note-off Damp (Rosewood). "
             "These comparisons reveal timbre and response; they do not establish acoustic equivalence to a recorded instrument.", "",
             "Classical MIDI adaptations retain their source credits and share-alike licences; see [third-party notices](../../THIRD_PARTY_NOTICES.md). "
             "Existing soundfont reference recordings remain unmodified and are not incorporated into the synthesis or these renders.", "",
             "| Track | Model | Striker / position | Duration | Native render | Peak / RMS | Decoded AAC peak |",
             "|---|---|---|---:|---:|---:|---:|"]
    for r in results:
        lines.append(f"| {r['song_title']} | {r['model']} | {r['striker']} / {r['strike_position'] * 100:g}% | "
                     f"{r['audio_duration_sec']:.1f}s | {r['real_time_factor']:.1f}× | "
                     f"{r['peak_db']:.1f} / {r['rms_db']:.1f} dBFS | {20 * math.log10(r['aac_decoded_peak']):.1f} dBFS |")
    REPORT.write_text("\n".join(lines) + "\n")


def render_jobs(duration, full, oversampling):
    """Production render plan, inspectable without generating or encoding audio."""
    for key, song in SONGS.items():
        for model in MODELS:
            yield (key, song["title"], model, model["default_mallet"], "", "Showcase",
                   3600 if full else duration, oversampling)
    for study in STRIKER_COMPARISONS:
        model = MODELS[study["model_id"]]
        key = study["song_key"]
        for mallet, tag, name in study["mallets"]:
            yield (key, SONGS[key]["title"], model, mallet, "_" + tag, "Strikers",
                   3600 if full else duration, oversampling)
    for model in MODELS:
        positions = ((0.50, "middle"), (0.35, "lower tine"), (0.10, "inner contact"), (0.90, "near tip")) if model["id"] == 3 else (
            (0.50, "centre"), (0.35, "off-centre"), (0.2242, "fundamental node"), (0.10, "edge"))
        for position, label in positions:
            tag = f"{position * 100:g}".replace(".", "_")
            yield ("06_Position_Study", f"C4 · {label} ({position * 100:g}%)", model,
                   1, f"_Position{tag}", "Touch", 6, oversampling, position)
        yield ("07_Dynamics_Study", "C4 · pp to ff (16 → 127)", model,
               1, "", "Touch", 8, oversampling, 0.5)
    for mallet, tag, name in STRIKER_COMPARISONS[1]["mallets"]:
        yield ("08_Pluck_Study", f"C4 · {name}", MODELS[3],
               mallet, "_" + tag, "Touch", 6, oversampling, 0.5)
    for model in MODELS:
        # Last hit at 5.5s plus the existing 3s acoustic tail gives 8.5s of audio.
        yield (REPEATED_STUDY_KEY, "Repeated C4 · fixed velocity (90)", model,
               model["default_mallet"], "", "Touch", 9, oversampling, 0.5)
    for key, title, model_id, mallet, duration, options in IDIOM_STUDIES:
        yield (key, title, MODELS[model_id], mallet, "", IDIOM_CATEGORY, duration, oversampling, None, options)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=45)
    parser.add_argument("--full", action="store_true", help="Render full compositions (player may become large)")
    parser.add_argument("--oversampling", type=int, choices=[0, 1, 2], default=0)
    parser.add_argument("--download-missing", action="store_true")
    args = parser.parse_args()
    if not math.isfinite(args.duration) or not 4 <= args.duration <= 3600:
        parser.error("duration must be finite and between 4 and 3600 seconds")
    if not shutil.which("ffmpeg"):
        parser.error("ffmpeg is required for reproducible AAC encoding and decode verification")
    AUDIO_DIR.mkdir(exist_ok=True)
    ensure_midi_files(args.download_missing)
    convert_midi_to_events()
    compile_renderer()
    initial_dsp_hashes = {str(p): sha256(p) for p in DSP_DIR.iterdir() if p.suffix in (".h", ".cpp")}
    results = [render_one(*job) for job in render_jobs(args.duration, args.full, args.oversampling)]
    dsp_files = sorted(p for p in DSP_DIR.iterdir() if p.suffix in (".cpp", ".h"))
    if {str(p): sha256(p) for p in dsp_files} != initial_dsp_hashes:
        raise RuntimeError("DSP changed during generation; rebuild and rerun to keep provenance honest")
    inputs = {str(p.relative_to(PROJECT_DIR)): sha256(p) for p in [*dsp_files, BASE_DIR / "maremba_renderer.cpp",
              BASE_DIR / "run_benchmark.py", BASE_DIR / "player_template.html", *sorted(MIDI_DIR.glob("*.mid")),
              *sorted(EVENTS_DIR.glob("*.events"))]}
    dsp_digest = hashlib.sha256("".join(f"{p.name}:{sha256(p)}\n" for p in dsp_files).encode()).hexdigest()
    manifest = {"schema_version": 1, "dsp_sha256": dsp_digest, "machine": platform.platform() + " / " + platform.machine(),
                "compiler": subprocess.run(["clang++", "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0],
                "encoder": subprocess.run(["ffmpeg", "-version"], capture_output=True, text=True, check=True).stdout.splitlines()[0],
                "inputs_sha256": inputs, "renders": results}
    selected = generate_players(results, manifest)
    manifest["embedded_aac_files"] = [r["aac"] for r in selected]
    manifest["player_sha256"] = sha256(PLAYER)
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    write_report(results, manifest)
    print(f"Verified {len(results)} WAV/AAC pairs; embedded {len(selected)} in {PLAYER.name}", flush=True)


if __name__ == "__main__":
    main()
