#!/usr/bin/env python3
"""Write the committed demo phrases, Docs/audio/midi/<NN>-<slug>.mid.

Eight of the 20 factory presets (README G3, G3a), each showing a different
side of the instrument: the concert marimba in a cathedral room (Ambient
Cathedral Bloom), the reference rosewood bar (Concert Grand Rosewood), hard
acrylic mallets (Hard Acrylic Virtuoso) and the chorale voicing (Rosewood Roll
Chorale), the Guatemalan charleo, the Birifor gyil and Chopi timbila balafons,
and the kalimba (Kalimba Pick Sparkle). NN is the program number in
Source/FactoryPresets.h, so a renamed preset fails here.

Each demo is the opening and the cadence of an eight-bar (six in 12/8)
miniature written for its preset: the first two to three bars, then the last
bar with its ritardando. The whole miniature is composed and the bars between
are dropped, so the cadence keeps the seeded hand timing it was written with;
a note still ringing at the cut stops there. Pop and African-inspired pieces
are original; the classical themes are independently entered public-domain
melodies with newly written cadences, and no existing MIDI performance is
used. The same miniatures, complete, are the Rack repository's preset
sessions (Examples/Maremba/Tools/PresetSessions/compositions.py).

The phrases play notes only, through MarembaPlayer as the plug-in does: the
key is the bar struck and the velocity its strike force. The balafon and
timbila presets keep their own non-equal tunings, so their phrases use the
tunings' degree keys: C D E G A for five tones, white keys for seven.

Each file is format 1 at 960 PPQ: a conductor track (title, credit, preset,
meter, tempo map, section markers), then one track per part (Bass, Harmony,
Melody, Reply) that the miniature has, even where the cut leaves it empty.
Every phrase's last event falls at 8.9-9.7 s. The renderer adds a 2.5 s tail
and fades it at 12 s, so the two phrases past 9.5 s (Ambient Cathedral Bloom
and Concert Grand Rosewood) lose at most the last 0.3 s of it; a phrase past
9.8 s fails here.

The files are sources, not build outputs: rerun this only to change a phrase
on purpose, and commit the result in its own commit. --check fails unless the
committed files match this script byte for byte.
"""
import argparse
import math
from pathlib import Path
import random
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Docs/audio/midi"
PPQ = 960


def pitch(name):
    if isinstance(name, int):
        return name
    match = re.fullmatch(r"([A-G])([#b]?)(-?\d+)", name)
    if not match:
        raise ValueError(name)
    letter, accidental, octave = match.groups()
    return 12 * (int(octave) + 1) + dict(C=0, D=2, E=4, F=5, G=7, A=9, B=11)[letter] + {"": 0, "#": 1, "b": -1}[accidental]


def pitches(names):
    return [pitch(n) for n in names.split()]


def vlq(value):
    result = [value & 127]
    while value >> 7:
        value >>= 7
        result.insert(0, (value & 127) | 128)
    return bytes(result)


def chunk(events):
    data, previous = bytearray(), 0
    for tick, priority, message in sorted(events, key=lambda item: (item[0], item[1])):
        data.extend(vlq(tick - previous) + message)
        previous = tick
    data.extend(b"\x00\xff\x2f\x00")
    return b"MTrk" + struct.pack(">I", len(data)) + data


def meta(kind, text):
    data = text.encode("utf-8")
    return b"\xff" + bytes([kind]) + vlq(len(data)) + data


class Piece:
    def __init__(self, title, preset, bpm, meter, bars, credit="Original composition and performance for Maremba."):
        self.title, self.preset, self.bpm, self.meter, self.bars, self.credit = title, preset, bpm, meter, bars, credit
        self.notes = []
        self.rng = random.Random(sum(map(ord, title)))
        self.bar_length = meter[0] * 4 / meter[1]
        self.tempos = [(0, bpm)]

    def add(self, beat, note, velocity=76, duration=.4, role="melody", delay=0):
        # A few milliseconds of intentional hand separation, with a stable
        # seed. No large random grid drift or random notes/velocities.
        seconds_jitter = self.rng.uniform(-.0028, .0028)
        role_delay = {"melody": .005, "harmony": .001, "bass": -.002, "reply": .003}.get(role, 0)
        tick = round((beat + .14 * self.bpm / 60 + (seconds_jitter + role_delay + delay) * self.bpm / 60) * PPQ)
        vel = max(1, min(119, round(velocity + self.rng.uniform(-1.8, 1.8))))
        self.notes.append([max(0, tick), pitch(note), vel, max(20, round(duration * PPQ)), role])

    def chord(self, beat, notes, velocity=65, duration=.55, role="harmony", spread=.006):
        for i, note in enumerate(pitches(notes) if isinstance(notes, str) else notes):
            self.add(beat, note, velocity - 1.5 * i, duration, role, delay=i * spread)

    def line(self, bar, notes, rhythm, velocity=86, role="melody", gate=.78):
        for index, (note, item) in enumerate(zip(pitches(notes), rhythm)):
            offset, duration = item if isinstance(item, tuple) else (item, .5)
            accent = [0, -5, -2, 2, -4, -1, -6, 1][index % 8]
            self.add(bar * self.bar_length + offset, note, velocity + accent, duration * gate, role)

    def finish(self, slowdown):
        """A ritardando over the last bar."""
        last = (self.bars - 1) * self.bar_length
        self.tempos.extend([(last, round(self.bpm * (1 - slowdown / 2))),
                            (last + self.bar_length / 2, round(self.bpm * (1 - slowdown)))])
        return self

    def finalized(self):
        # A MIDI key cannot have independent note-offs on a single device.
        # Resolve near-unisons, then end held keys before their next attack.
        notes = []
        for note in sorted(self.notes, key=lambda n: (n[0], n[1])):
            duplicate = next((n for n in reversed(notes[-12:]) if n[1] == note[1] and abs(n[0] - note[0]) < 16), None)
            if duplicate is not None:
                duplicate[2] = max(duplicate[2], note[2])
                duplicate[3] = max(duplicate[3], note[3])
            else:
                notes.append(note.copy())
        for key in range(128):
            same = [n for n in notes if n[1] == key]
            for a, b in zip(same, same[1:]):
                a[3] = min(a[3], b[0] - a[0] - 2)
                if a[3] < 1:
                    raise ValueError("Overlapping same-key attacks")
        return sorted(notes, key=lambda n: (n[0], n[1]))

    def demo(self, keep):
        """The SMF of the first `keep` bars and the cadence bar."""
        notes = self.finalized()
        bar = round(self.bar_length * PPQ)
        cut, cadence = round(keep * bar), (self.bars - 1) * bar
        shift = cadence - cut
        kept = []
        for tick, note, vel, length, role in notes:
            if tick < cut:
                kept.append((tick, note, vel, min(length, cut - tick), role))
            elif tick >= cadence:
                kept.append((tick - shift, note, vel, length, role))
        tempo_map = sorted((round(beat * PPQ), round(60_000_000 / bpm)) for beat, bpm in self.tempos)
        tempo_map = [(tick - shift if tick >= cadence else tick, tempo) for tick, tempo in tempo_map]

        def seconds(tick):
            previous, tempo, elapsed = 0, tempo_map[0][1], 0.
            for change, new_tempo in tempo_map[1:]:
                if change > tick:
                    break
                elapsed += (change - previous) * tempo / (PPQ * 1e6)
                previous, tempo = change, new_tempo
            return elapsed + (tick - previous) * tempo / (PPQ * 1e6)

        last = seconds(max(tick + length for tick, _, _, length, _ in kept))
        if not 5.5 <= last <= 9.8:
            raise ValueError(f"{self.preset}: its last event at {last:.2f} s is outside 5.5-9.8 s")
        conductor = [(0, -5, meta(3, self.title)), (0, -4, meta(1, self.credit)),
                     (0, -3, meta(1, f"Maremba preset: {self.preset}; set Quality to 8x.")),
                     (0, -2, b"\xff\x58\x04" + bytes([self.meter[0], int(math.log2(self.meter[1])), 24, 8]))]
        conductor += [(tick, -1, b"\xff\x51\x03" + value.to_bytes(3, "big")) for tick, value in tempo_map]
        conductor += [(0, 0, meta(6, "Opening")), (cut, 0, meta(6, "Cadence"))]
        tracks = [chunk(conductor)]
        for role in ("bass", "harmony", "melody", "reply"):
            if any(part == role for *_, part in notes):
                events = [(0, -1, meta(3, role.title()))]
                for tick, note, vel, length, part in kept:
                    if part == role:
                        events += [(tick, 1, bytes([0x90, note, vel])), (tick + length, 0, bytes([0x80, note, 0]))]
                tracks.append(chunk(events))
        return b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), PPQ) + b"".join(tracks)


def pop(title, preset, bpm, harmony, melodies, rhythm, accompany, slowdown=.07):
    # Each eight-bar hook has a recognisable opening, an answer, a small lift
    # after the midpoint, and a written tonic ending. Chord voicings are kept
    # below the melody; bass, inner voices and tune have separate dynamics.
    p = Piece(title, preset, bpm, (4, 4), 8)
    for bar, (h, tune) in enumerate(zip(harmony, melodies)):
        bass, *voicing = pitches(h)
        if preset.startswith("Kalimba"):  # no tines below A3
            while bass < 57:
                bass += 12
            voicing = [n + (12 if n < 60 else 0) for n in voicing]
        b = bar * 4
        lift = [0, -3, 0, -4, 5, 3, 1, -9][bar]
        p.add(b, bass, 71 + lift, .5, "bass")
        if bar == 7:
            p.chord(b + .48, voicing, 55, 2., spread=.016)
            ending = [(0, .6), (.75, .55), (1.5, .5), (2.25, .4), (3, 1.)]
            p.line(bar, tune, ending, 78, gate=.9)
            p.add(b + 3, bass, 62, 1.1, "bass")
            continue
        accompany(p, b, bass, voicing)
        p.line(bar, tune, rhythm, 86 + lift, gate=.84)
        if bar in (3, 5):
            p.add(b + 3.83, voicing[-1], 54, .12, "reply")
    return p.finish(slowdown)


def thumb_comping(p, b, bass, voicing):
    for offset, idx in [(.5, 0), (1.25, 1), (2, 2), (2.5, 0), (3.25, 1)]:
        p.add(b + offset, voicing[idx], 50 + 3 * idx, .32, "harmony")
    p.add(b + 2, bass + 7, 58, .4, "bass")


def soul_comping(p, b, bass, voicing):
    p.chord(b + .55, voicing, 57, 1.2, spread=.012)
    p.chord(b + 2.8, voicing[:2], 51, .7)
    p.add(b + 2, bass + 12, 56, .7, "bass")


def bossa_comping(p, b, bass, voicing):
    for offset, inversion in [(1, 0), (2.5, 1), (3.5, 0)]:
        p.chord(b + offset, voicing[inversion:inversion + 2], 56 + (3 if offset == 2.5 else 0), .32)
    p.add(b + 2.75, bass + 7, 63, .4, "bass")


def paper_lanterns():
    # A bright thumb-piano pop hook: answering thirds, a lifted second phrase
    # and a little falling signature.
    return pop("Paper Lanterns", "Kalimba Pick Sparkle", 112,
               ["C4 E4 G4 B4", "A3 E4 G4 C5", "F3 F4 A4 C5", "G3 D4 G4 B4", "C4 E4 G4 B4", "A3 E4 A4 C5", "G3 F4 G4 B4", "C4 E4 G4 C5"],
               ["E5 G5 E5 D5 C5", "E5 A5 G5 E5 D5", "F5 A5 G5 E5 C5", "D5 E5 D5 B4 G4", "E5 G5 B5 A5 G5", "E5 A5 G5 E5 C5", "D5 G5 F5 D5 B4", "C5 E5 G5 E5 C5"],
               [(0, .65), (.75, .6), (1.5, .8), (2.75, .4), (3.5, .35)], thumb_comping)


def velvet_postcard():
    # A warm pop ballad with suspended harmony, singing rosewood and a soft,
    # resolved final answer.
    return pop("Velvet Postcard", "Concert Grand Rosewood", 104,
               ["F3 A3 C4 E4", "D3 A3 C4 F4", "Bb2 A3 D4 F4", "C3 Bb3 D4 E4", "F3 A3 C4 E4", "Bb2 A3 D4 F4", "C3 G3 Bb3 E4", "F3 A3 C4 F4"],
               ["A4 C5 D5 C5 A4", "A4 F5 E5 D5 C5", "D5 F5 E5 C5 A4", "G4 Bb4 A4 G4 E4", "A4 C5 F5 E5 D5", "D5 F5 G5 F5 D5", "E5 D5 C5 Bb4 G4", "A4 C5 A4 G4 F4"],
               [(0, 1.2), (1.5, .6), (2.25, .45), (2.75, .5), (3.5, .4)], soul_comping, slowdown=.14)


def palm_shadow_polaroid():
    # A relaxed wooden pop melody with a gently anticipated bass and a
    # brighter phrase in the upper register.
    return pop("Palm-Shadow Polaroid", "Guatemalan Charleo", 112,
               ["C3 E4 G4 B4", "A2 E4 G4 C5", "D3 F4 A4 C5", "G2 F4 A4 B4", "E3 E4 G4 B4", "A2 E4 G4 C5", "G2 F4 G4 B4", "C3 E4 G4 C5"],
               ["G5 E5 D5 E5 C5", "E5 G5 A5 G5 E5", "F5 A5 G5 F5 E5", "D5 F5 E5 D5 B4", "G5 B5 A5 G5 E5", "A5 G5 E5 D5 C5", "D5 G5 F5 D5 B4", "C5 E5 D5 G4 C5"],
               [(.5, .6), (1.25, .5), (2, .6), (2.75, .4), (3.5, .35)], bossa_comping)


def bach_in_cut_glass():
    # Bach's C-major Prelude opening, recast as a bright mallet miniature with
    # a newly written dominant-to-tonic close. The first four measures follow
    # the Prelude's opening arpeggio; five to eight condense the journey into
    # a self-contained cadence.
    p = Piece("Bach in Cut Glass", "Hard Acrylic Virtuoso", 98, (4, 4), 8,
              "J. S. Bach, Prelude in C major, BWV 846: opening harmony and figure; new miniature arrangement and cadence.")
    arps = ["C3 E3 G3 C4 E4", "C3 D3 A3 D4 F4", "B2 D3 G3 D4 F4", "C3 E3 G3 C4 E4",
            "A2 E3 A3 C4 E4", "F2 A3 C4 F4 A4", "G2 B3 D4 F4 G4", "C3 E3 G3 C4 E4"]
    for bar, chord in enumerate(arps):
        ns = pitches(chord)
        order = [0, 1, 2, 3, 4, 2, 3, 4, 0, 1, 2, 3, 4, 2, 3, 4]
        if bar == 7:
            order = order[:8]
        for i, degree in enumerate(order):
            p.add(bar * 4 + i / 4, ns[degree], (72 if degree >= 3 else 59) + (4 if bar == 4 else 0),
                  .28, "melody" if degree >= 3 else "harmony")
        if bar == 7:
            p.chord(30.35, "C3 G3 E4 C5", 67, 1.4, spread=.018)
    return p.finish(.16)


def joy_in_rosewood():
    # Ode to Joy carried by a clear upper voice over soft alternating mallets,
    # growing once before its final cadence.
    p = Piece("Joy, in Rosewood", "Rosewood Roll Chorale", 112, (4, 4), 8,
              "L. van Beethoven, Symphony No. 9, Op. 125: Ode to Joy melody; new mallet chorale accompaniment.")
    lines = ["F#4 F#4 G4 A4", "A4 G4 F#4 E4", "D4 D4 E4 F#4", "F#4 E4 E4",
             "F#4 F#4 G4 A4", "A4 G4 F#4 E4", "D4 D4 E4 F#4", "E4 D4 D4"]
    harmonies = ["D3 A3 D4", "A2 A3 C#4", "G2 B3 D4", "A2 G3 C#4",
                 "D3 A3 D4", "A2 A3 C#4", "G2 B3 D4", "D3 A3 F#4"]
    for bar, (line, harmony) in enumerate(zip(lines, harmonies)):
        ns = pitches(harmony)
        p.add(bar * 4, ns[0], 60, 1, "bass")
        for i in range(8 if bar < 7 else 5):
            p.add(bar * 4 + i * .5 + .04, ns[1 + i % 2], 46 + 3 * (i % 2), .24, "harmony")
        r = [(0, 1), (1, 1), (2, 1), (3, 1)] if len(line.split()) == 4 else [(0, 1.5), (1.5, .5), (2, 2)]
        p.line(bar, line, r, 84 + (5 if bar == 4 else 0) - (9 if bar == 7 else 0), gate=.92)
    return p.finish(.15)


def canon_at_dusk():
    # Pachelbel's descending Canon line floats above open voicings; a
    # restrained final D chord lets the hall breathe.
    p = Piece("Canon at Dusk", "Ambient Cathedral Bloom", 108, (4, 4), 8,
              "Johann Pachelbel, Canon in D: ground bass and descending theme; new short arrangement with tonic close.")
    harmony = ["D3 F#4 A4", "A2 E4 A4", "B2 D4 F#4", "F#2 C#4 F#4",
               "G2 B3 D4", "D3 A3 D4", "G2 B3 D4", "A2 C#4 E4"]
    melody = ["F#5 E5", "D5 C#5", "B4 A4", "B4 C#5", "D5 C#5", "B4 A4", "G4 F#4", "E4 D4"]
    for bar, (h, m) in enumerate(zip(harmony, melody)):
        bass, *inner = pitches(h)
        p.add(bar * 4, bass, 59, 1.6, "bass")
        p.chord(bar * 4 + .5, inner, 46, 1.4, spread=.02)
        p.line(bar, m, [(0, 2), (2, 2)], 76 - (6 if bar == 7 else 0), gate=.9)
        if bar < 7:
            p.add(bar * 4 + 2.75, inner[-1], 46, .7, "reply")
    p.chord(30.05, "D3 A3 F#4", 53, 2.0, spread=.024)
    return p.finish(.20)


def interlock(title, preset, bpm, keys, motifs):
    # A newly composed interlocking study in 12/8, not a transcription of
    # named traditional repertoire: a low anchor, a lead motif lifted an
    # octave in the middle bars and an answering reply, closing on a unison
    # gesture across the octaves.
    p = Piece(title, preset, bpm, (12, 8), 6)
    pcs = [pitch(k + "4") % 12 for k in keys.split()]
    for i in range(1, len(pcs)):
        while pcs[i] <= pcs[i - 1]:
            pcs[i] += 12

    def degree(i, octave=4):
        return (octave + 1 + i // len(pcs)) * 12 + pcs[i % len(pcs)]

    for bar in range(p.bars):
        start, length = bar * p.bar_length, p.bar_length
        final = bar == p.bars - 1
        lift = 4 if p.bars // 2 <= bar < p.bars - 2 else 0
        p.add(start, degree([0, 0, 1, 0][bar % 4], 3), 72 + lift - (12 if final else 0), .5, "bass")
        if final:
            motif = [3, 2, 1, 0]
            lead_offsets = [length * v for v in [.08, .24, .4, .58]]
            answer_offsets = [length * .16, length * .48]
        else:
            p.add(start + length / 2, degree(3 if bar % 2 == 0 else 2, 3), 64 + lift, .38, "bass")
            motif = motifs[bar % 4]
            lead_offsets = [.5, 1.5, 2.5, 3.5, 4.5, 5.25]
            answer_offsets = [0, 1, 2, 3, 4, 5.5]
        for i, at in enumerate(lead_offsets):
            d = motif[i % len(motif)]
            if p.bars // 2 <= bar < p.bars - 2 and i == 2:
                d += len(pcs)
            p.add(start + at, degree(d), 82 + lift + [3, -7, 0, -4, 2, -8][i % 6] - (13 if final else 0), .33, "melody")
        for i, at in enumerate(answer_offsets):
            d = [0, 2, 1, 3, 2, 0][(i + bar % 2) % 6]
            p.add(start + at + .03, degree(d, 3 if i % 3 == 0 else 4), 54 + (5 if i % 2 == 0 else 0), .28, "reply")
        if final:
            p.chord(start + length * .72, [degree(0, 3), degree(0), degree(3)], 63, 1., spread=.016)
    return p.finish(.09)


def copper_circles():
    # A brisk two-part weave in twelve, with close answering figures and a
    # final unison gesture across the octaves.
    return interlock("Copper Circles", "Chopi Timbila Ensemble", 118, "C D E F G A B",
                     [[0, 3, 2, 5, 4, 2], [1, 4, 3, 6, 5, 3], [2, 5, 4, 7, 6, 4], [4, 3, 2, 1, 2, 0]])


def ochre_sky():
    # A rounded five-tone ostinato under a more spacious singing answer; the
    # preset's measured tuning supplies its colour.
    return interlock("Ochre Sky", "Birifor Gyil Buzz", 116, "C D E G A",
                     [[0, 2, 3, 4, 2, 1], [1, 3, 4, 3, 1, 0], [2, 4, 5, 4, 3, 2], [3, 2, 1, 2, 1, 0]])


# The demos and how many opening bars each keeps before its cadence.
DEMOS = [
    (canon_at_dusk, 3),
    (velvet_postcard, 3),
    (bach_in_cut_glass, 2.5),
    (joy_in_rosewood, 3),
    (palm_shadow_polaroid, 3),
    (ochre_sky, 2),
    (copper_circles, 2),
    (paper_lanterns, 3),
]


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def files():
    bank = re.findall(r'FactoryPreset \{ "([^"]+)"', (ROOT / "Source/FactoryPresets.h").read_text())
    result = {}
    for compose, keep in DEMOS:
        piece = compose()
        result[f"{bank.index(piece.preset) + 1:02d}-{slug(piece.preset)}.mid"] = piece.demo(keep)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail unless the committed files match")
    args = parser.parse_args()
    expected = files()
    found = {path.name: path.read_bytes() for path in OUT.glob("*.mid")}
    if args.check:
        stale = sorted(set(found) ^ set(expected) | {name for name in expected if found.get(name) != expected[name]})
        if stale:
            sys.exit(f"error: Docs/audio/midi differs from {Path(__file__).name} in: {', '.join(stale)}")
        print(f"{len(expected)} demo phrases match {Path(__file__).name}")
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for name in set(found) - set(expected):
        (OUT / name).unlink()
    for name, data in expected.items():
        (OUT / name).write_bytes(data)
    print(f"wrote {len(expected)} demo phrases to {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
