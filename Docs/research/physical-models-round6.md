# Sixth physical-model pass — research-based fixes (1.4.0b1)

> **1.4.0:** the **Bow** described below was removed before release. The Striker
> row keeps the four heads and the Shaft; a Bow stored by the 1.4.0b1 beta plays
> as the Shaft, and the Bowed Rosewood Choir patch became Rosewood Roll Chorale
> (soft yarn, for mod-wheel rolls). This record keeps the bow's design as it was.

> 1.4.0b1 combines this pass with the 1.3.0f2–f5 line developed alongside it
> (Buzz range, Strike Variance curve, mounted kalimba shapes, membrane
> material, the feedback update's bank rules): see
> [combined models](combined-models-1.4.0b1.md). Where they differ, that page
> describes the engine.

This pass works through the prioritized evaluation that followed the
[fifth pass](physical-models-round5.md): each item was researched first
([research notes](physical-model-round6-references.md)) and then fixed,
changed on the evidence, or left open where the evidence did not hold up. It
adds two front controls (**Carving**, **Tension**, MIDI ids 291 and 292), two
strikers (**Shaft**, **Bow**), a third Note Off mode (**Dead**) and two
measured keyboards (**Gyil**, **Valimba**). Every stored property, range and
automation id is kept; stepped properties only gained steps at the end of their
lists, as the SDK's update rules allow.

No real instrument was recorded for this pass. The numbers below compare the
model with published measurements; they are not a listening test.

## What changed

| Item | Research finding | Change |
| --- | --- | --- |
| Tube Q | Soares et al.'s Q 68–53 were one tube's first four modes, not tubes for those notes. The tube bank's bores with wall and mouth loss (× 0.8, their measured tube) give Q 87 at C2, 63 at C4, 32 at C5, 3 at C7 | `ResonatorQ` follows that law (was 58 / 44 / 37 / 15) |
| C2 decay share | Hill: 0.54 | 0.56 (was 0.42) |
| Bass bloom height | References: G3 +8.8, C4 +5.7 dB | +8.9, +6.3 (was +3.6, +3.6); the bass bloom now peaks later than the references |
| Tube tuning | Makers set tubes slightly above their bars, more than a half step up in the top octave | +3 cents at C2, +8 through C4–C5, +100 at C7 (was −1, −9, −33) |
| Guatemalan bars | Traditional builders tune the fundamental only; overtone tuning belongs to the concert-style instruments without the charleo | Hormigo bars carved to the fundamental: the first overtone falls from 4.0 in the bass to 3.54 at C4 and 2.9 at C6 |
| Overtone tuning control | — | **Carving**: 0 leaves every key uncarved (2.76 / 5.40 × f1), 100 % is the key as built |
| Balafon keys | Keys follow a length progression; their overtones come from the carving | Lengths within 10 % of the gyil's law; first overtones 2.55–3.58 × f1 |
| Balafon playing position | Godlovitch: the first overtone about 14 dB down | Factory patches strike at 0.43: −10 to −13 dB from G3 to C♯4, falling above |
| Buzz controls | Membrane threshold ~ tension^1.5, formant ~ √tension | **Tension**: half an octave of formant either side of the voicing, threshold with it |
| Buzz noise floor | Godlovitch: about 5 dB of broadband buzz over 1–6 kHz | Each slap finds its taut limit anew (±15 % balafon, ±10 % charleo); its own generator keeps it rate-invariant |
| Balafon top keys | Voisin: the top of a balafon carries no membrane | No membranes from C6 up |
| Strikers | Col legno on the bar's edge; bowed bars (Essl & Cook) | **Shaft** (a fingernail **Tap** on the kalimba) and **Bow** |
| Dead stroke | "Hold it firmly into the bar" | **Note Off: Dead** |
| Muffling | Burritt's muffled low E decays 4–5× faster; a thumb stops a tine in a few periods | A hand across the bar (per-mode, the tube under it to Q 35) and a one-sided thumb on the tine, both with a soft landing |
| Kalimba hole cover | Benade & Murday's end correction for a disk over a hole | The wheel lowers a palm from 40 to 1 mm over the front hole |
| Traditional tunings | Measured keyboards and per-key spreads | **Gyil** (Godsey) and **Valimba** (A. Tracey) scales; Bala, Pentatonic and Gyil keys keep their own deviation |
| Decay | — | Decay stretches the bar, its air and their exchange together, so a tube-carried bass note lengthens too |
| CPU spike of a fresh chord | — | Each key's radiation readouts are kept per MIDI note, the resonator tuner stops at a bass key's jump, and the note-on trigonometry vectorizes: a 16-note chord on keys played before costs 0.58–0.60 ms (was 0.91–1.05) |

Left open, with the evidence:

- **The gyil's 0.75 f0 component.** Godlovitch's gourded key shows a line at
  0.75 f0 (−10.7 dB). A gourd tuned a fourth below its key would explain it, but
  in the model such a gourd gave the line only 77 dB down and took the key's
  strong buzz away; no mechanism found gives both.
- **Scala import.** A Rack Extension cannot read user files or receive SysEx,
  so no tuning file or MIDI tuning message can reach it. The measured keyboards
  are compiled in.
- **Kalimba "tink".** The earlier comparison was against a board kalimba;
  the tink depends on technique. A nail at the tip sounds the tine's second
  mode 32 dB more than flesh inside it (contract below); nothing was changed.
- **Mirliton compliance.** A dizi's membrane lowers its tube 20–70 cents; no
  gourd measurement exists, and the effect confounded Buzz as an added layer.
  Not modelled; it lies within the gourds' own ±30-cent scatter.

## Tubes

Only the lowest air mode of each tube takes part, as before. Its Q now follows
the wall and mouth losses of La Favre's 5-octave bank (98 mm bores at C2, 54 mm
through C4–C♯5, 48 mm from D5), 0.8 of the theory as Soares et al. measured on
their tube (68 at 174 Hz; the law gives 67.8 there):

| | C2 | C3 | C4 | A4 | C5 | C6 | C7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Q, 1.4.0b1 before | 58 | 51 | 44 | 39 | 37 | 27 | 15 |
| Q now | 87 | 79 | 63 | 40 | 32 | 10.5 | 3 |

The coupling is unchanged, so the bar and its tube now couple past critical
from C2 to C4: their twin lines beat slowly, as the reference G3 shows (a twin
23 cents off). C2 keeps 0.56 of its bare decay (Hill 0.54). Blooms against the
reference notes (fitted by `Demos/fit_modal_reference.py`):

| | G3 | C4 | E4 | G4 | C5 | E5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Reference rise | 8.8 dB | 5.7 | 3.8 | 3.2 | 1.2 | 0.1 |
| Model rise | 8.9 dB | 6.3 | 4.3 | 2.6 | 0.5 | 0.0 |
| Reference peak | 104 ms | 76 | 74 | 54 | 37 | 22 |
| Model peak | 149 ms | 104 | 77 | 58 | 33 | 18 |

An extra in-situ tube loss was tried against all of Hill's ratio, Burritt's
muffling and these blooms: it brings the bass peaks earlier but lowers the
rises; the unmodified law matches the rises best and was kept.

Tubes sit +3 cents above their bars at C2 and +8 cents through C4–C5, rising
to +100 cents at C7 (loaded, as a maker checks by blowing into them with the
bars in place). The pitch compensation holds the key within 0.13 cent of pitch
across Coupling 0–1 at the default Tune. A tube Tuned 50 cents further sharp
rings on at its own pitch where it outlasts a weakly coupled bar (Coupling
0.35 at C3–D3): up to 21 cents, as a mistuned tube does.

**Decay** now stretches the whole resonant system: the bar's loss, the air's
loss and their exchange scale together, so a bass note carried by its tube
lengthens with the knob and the bloom keeps its shape; its timing stretches
with it (at Decay 2.5 a C3's bloom peaks near 0.35 s). Scaling the losses
alone would keep the bloom's timing, but the pair would then beat deeper at
long settings and its heard pitch would drift (6.5 cents sharp at A♯4, Decay
0.4).

## Bars

`Tests/generate_bar_modes.py` now solves its eigenproblems densely: ARPACK's
shift-invert iteration resolved f1 to only about 10⁻⁷, which spoiled the tuning
fits' finite-difference steps and left rosewood C♯4–F♯5 at 3.5:1 instead of
4:1. Its assembly is vectorized (the run takes about four minutes). Mode and
torsion ratios interpolate geometrically between the anchors.

- **Hormigo**: carved to the fundamental only with a moderate central arch, as
  traditional Guatemalan builders do. First overtone: 4.19 (C1), 4.00 (C2), 3.78
  (C3), 3.54 (C4), 3.26 (C5), 2.91 (C6).
- **Balafon**: each key's length stays within 10 % of the gyil's progression and
  its carving comes as close to its own overtone target as that allows: 2.55–3.58
  × f1 across C2–C7.
- **Carving** (front, Resonance row, 0–100 %, default 100 %): each key is also stored uncarved,
  a bar of its length and width thinned evenly to the note (2.76 and 5.40 × f1).
  In between the undercut deepens: the ratios move geometrically, the mass,
  shapes and thickness linearly; the fundamental stays the note. The kalimba's
  tines have no undercut.
- The cords sit at each bar's own fundamental node (wherever the carving put it).

## Strikers

The Striker row gains two steps:

| Step | Bars | Kalimba |
| --- | --- | --- |
| Shaft / Tap | The mallet's shaft on the bar's edge: Hertz contact of an 8–9 mm handle (K 2 × 10⁸ N/m^1.5, rattan to birch with Hardness), 27 g at the shoulder; 0.31 ms on a rosewood C4. The edge contact twists the bar, and the handle's three bending modes (95–1080 Hz) knock with it | A fingernail tap on the tine |
| Bow | A rosined double-bass bow on the bar's end | The same on the tine's tip |

**Bow.** Friction by Smith and Woodhouse's rosin law, solved every sample
(stick while the force that keeps the bar at bow speed stays under static
friction, else slip). Velocity sets the bow's speed (0.05–0.25 m/s), Hardness
its force (2–20 N), Position's upper half moves it from the end toward the
quarter point, where the second mode takes over. The hair lands in 4 ms and the
stroke draws in 40 ms; the mod wheel draws up to twice as fast and presses
harder with it (a +6 dB swell). A bowed rosewood C4 holds its level, sits on
its pitch and is periodic (a line at 2 f0, 28 dB down); a light, fast bow does
not speak, as Essl & Cook found. At key release the bow leaves the bar (Ring,
Damp) or stops on it (Dead).

## Note Off

| Mode | At the key's release |
| --- | --- |
| Ring | Nothing: the key rings on (bit-identical) |
| Damp | A hand across the bar's quarter-to-middle span (24 N s/m under it, each mode by its mean square there; the tube under it down to Q 35), landing softly; on the kalimba a thumb laid flat (a one-sided 3 kN/m, 2.4 N s/m contact pressing 1 N, landing over 6 ms) |
| Dead | The mallet stays pressed from the stroke (5 N, a gripped stick) and lifts at release; a plucked tine gets the thumb back after 30 ms; a tapped one keeps the fingertip; a bow stops on the bar |

Transport stop lays a lighter hand on every sounding key and on the keys
ringing along: 0.7 of the Damp hand's damping, the tube's extra loss with it,
so the stop is gentle (0.1–0.7 of the free ring 50–100 ms later) and every
voice is free within 3 s even at Decay 8.

| | Result |
| --- | --- |
| Muffled rosewood E2 | 65 dB/s against 13 free (5.0×; Burritt 4–5×) |
| Damped rosewood C4, 0.2 s after release | −34 dB |
| Kalimba E4, 30 ms after release | −56 dB |
| Dead stroke, rosewood C4 at 0.4 s | −36 dB against Ring; lifting leaves −56 dB re the stroke |

## Membranes

**Tension** (front, Character row, −100 … +100 %, default 0): a membrane's resonance goes as the
square root of its tension and its threshold as tension^1.5, so Tension moves
the buzz formant half an octave either side of the voicing and the threshold
with it (the formant ratio cubed). At a balafon A3's forte stroke the buzz layer
reads +1.3 dB (slack), +0.7 (as voiced) and −5.9 (tight) against the note.

Each slap finds its taut limit anew (±15 % on the balafon's silk, ±10 % on the
charleo), drawn from a per-slap generator so the sequence is the same at every
rate. The charleo's gap was re-voiced (0.80 → 0.88) for the new hormigo bars: soft
strokes at medium Buzz stay silent while the Guatemalan patches' Buzz is heard
at mezzo-forte (−34 dB at Buzz 0.35).

## Kalimba hole cover

`KalimbaHoleCover` models a palm (radius four times the hole's) over the 3 cm
front hole, which carries two thirds of the box's conductance: Benade and
Murday's end correction below 1.6 hole radii, and the radial flow through the
gap below 5 mm. The wheel lowers the palm from 40 mm to 1 mm on a log scale:

| Wheel | 0.25 | 0.5 | 0.75 | 1 |
| --- | ---: | ---: | ---: | ---: |
| Palm height | 16 mm | 6.3 mm | 2.5 mm | 1 mm |
| Box air mode | −54 cents | −150 | −465 | −751 |

## Tunings

| Scale | Now |
| --- | --- |
| Bala 7-equal | Each key keeps its own deviation, 6 cents (SD), as Rouget & Schwarz's balafons |
| Timbila | Unchanged: the Zavala orchestra, copied by octaves ("true and exact") |
| Pentatonic 5-equal | Each key keeps its own deviation, 25 cents (SD), as measured amadindas; the Senufo balafon is equipentatonic too (no per-key data exist) |
| Sambla | Unchanged degrees; the keys between them now fall halfway along |
| **Gyil** (new) | Godsey's averaged Birifor kogyil, fourteen keys from C3 on C D E G A (238-cent steps, octaves 1169–1205), plus the 30-cent (SD) deviation of an individual instrument |
| **Valimba** (new) | A. Tracey's Sena xylophone, keys 4–24 on the white keys from C3 exactly as measured (170-cent steps, uneven octaves, flat top keys) |

Deviations are drawn once per key (identical on every strike, reset and rate)
and only on the keys carrying the scale's degrees; the keys between them fall
halfway along, so chromatic runs keep rising. Beyond a measured range the
nearest measured octave repeats exactly.

## Factory patches

Two new patches (20 in all); every patch stores Carving (100 %) and Tension (0):

| Patch | Shows |
| --- | --- |
| Bowed Rosewood Choir | The bow on rosewood (Hardness 0.65, the bar's end) in the Chamber |
| Birifor Gyil Buzz | The Gyil scale, Buzz 0.60, a slightly slack membrane |

Two existing patches were re-levelled for the bank's voicing window
(`AllPatchRenderTest`: a phrase between −24 and −17 dBFS, forte chords below
0 dBFS on the direct outputs):

| Patch | Change | Why |
| --- | --- | --- |
| Equipentatonic Log Keys | Position 0.12 → 0.30, Volume 0.80 → 0.78 | A wood stick on the carved balafon key's thinned end gives a 0.1 ms click 57–61 dB above the ring (43 dB at the playing position): forte chords reached +10.7 dBFS on the direct outputs while the phrase fell to −24.9 dBFS. Between the fundamental's node and the centre it measures −22.9 dBFS, forte chords −0.7 dBFS |
| Direct Piezo Mallet | Volume 0.93 → 0.95 | The phrase sat at −24.0 dBFS; now −23.7 |

## CPU

Native clang -O3 on an Apple arm64 machine, 48 kHz, 64-frame blocks, 16 voices,
default 2x, thread CPU per block (`Demos/performance_review.cpp`; the fresh
chords with a probe that strikes 16 new keys over 16 ringing ones). The best of
three runs, as other work shared the machine:

| | Fifth pass | Sixth pass |
| --- | ---: | ---: |
| Rosewood / Padauk, median | 192 µs | 199–201 µs |
| Balafon, median | 218 µs | 230 µs |
| Kalimba, median | 142 µs | 144 µs |
| 16-note restrike burst every 32 blocks, p99, wooden models | 541–562 µs | 588–604 µs |
| The same burst, Kalimba | 212 µs | 217 µs |
| 16 fresh notes over 16 ringing ones: note-ons plus the next block, keys played before, wooden models | 0.91–1.05 ms | 0.58–0.60 ms |
| The same, keys not played since the model or Carving changed | 0.91–1.05 ms | 0.95–1.00 ms |
| Kalimba, keys played before / not | — | 0.20 / 0.49 ms |

A fresh strike projects every mode's radiation onto both microphones; the
engine now keeps each key's projection (per MIDI note, for the current model
and Carving) and a later strike of the key reuses it while its modes sit
within a stroke's pitch scatter. What remains of a fresh strike got cheaper
too: the resonator tuner used to run all 24 of its secant steps on a bass key
whose bar-like mode jumps across the note (seven in eight of Rosewood's keys
below E3) and now stops when the secant turns steeper than the branch can be
(the tuned pitch changes by at most 0.006 cents across 21 384 checked cases);
the radiation's and the upper mode shapes' sines and cosines use a
branch-free, vectorizing sincos accurate to 1 ulp. A fresh Rosewood strike
costs 25 µs (52 µs before), and 21 µs more in its first block while the
mallet is in contact. The steady load is 1–5 % above the fifth pass's. No block
missed its deadline (1.33 ms) in these runs; the first chord after loading a
patch comes within about 0.35 ms of it, as in the fifth pass. These are not
Rack Extension toolchain or Reason scheduler measurements.

## Validation

`bash Tests/run_all_tests.sh` passes all steps. Built with AddressSanitizer and
UndefinedBehaviorSanitizer, `test_dsp`, `Round5ResponseTest`, `Round6ResponseTest`
and `MalletContactTest` run clean (`test_dsp` needs more than the default 8 MB
stack there, as in CI, which links it with 32 MB: it keeps some 58 engines of
466 KB as locals, and the sanitizer gives each its own slot).
The demos were regenerated (`python3 Demos/run_benchmark.py`, Buzz 0 as before).
New:

- `Round6ResponseTest`: the tube law and Soares's tube, the tube tuning, Hill's
  C2 share, the bloom, Carving, Tension, the dead stroke and its lift, the
  muffling hand, the thumb, the bow (pitch, sustain, periodicity, release,
  swell), the shaft's contact, the valimba and gyil keyboards and every scale's
  per-key spread, the hole cover, the balafon's overtone at its playing
  position, membrane-free top keys and the kalimba tink.

Contracts changed, with reasons:

| Contract | Before | Now | Why |
| --- | --- | --- | --- |
| Hormigo tuning (`BarModesTest`) | 4:1 at C4 | first overtone 3.0–3.85 at C4, falling up the keyboard, above a flat bar's | Traditional bars are tuned in their fundamental only |
| Balafon overtone spread | 2.15–3.75 over C1–C8 | 2.3–3.75 over C2–C7 | Keys held near their length law; C1 and C8 are outside any balafon |
| Tunings (`Round5ResponseTest`) | every scale repeats at the octave, offsets ≤ 100 cents | Equal and Timbila repeat; every scale rises key by key, offsets ≤ 150 | Hand-tuned keys keep their own deviation; measured keyboards |
| C4–E4 interval target | from equal-tempered C4 | from the scale's own C4 | The same |
| Detuned resonator pitch | within 5 cents at every Tune | 5 cents at Tune 0, 25 at ±50 | A tube 58 cents sharp of its bar rings on at its own pitch |
| Pitch windows (`test_dsp`, wrapper) | 0.15–0.6 s and 0.15–1.0 s | 0.15–0.45 s, the tuner's (0.15–1.15 s below C3); rate check compares rates | Twin tube and bar lines beat past critical coupling |
| Transport stop (`test_dsp`) | voices freed 1.5 s after DampAll | the chord 60 dB down 1.5 s after, voices freed by 3 s | A hand stops a bass bar at 50–65 dB/s |
| Decay lengthens the tail (`PhysicalResponseTest`) | tail 0.30–0.60 s | bass (C3) tail 0.60–0.90 s; C5 unchanged | Decay stretches the bloom too: at 2.5 a C3's bloom peaks near 0.35 s, inside the old tail window |
| P2-2 buzz order (`test_dsp`) | the Balafon's key at C2, C4, C6 | the median of its five keys around C2, C4, G5 | Each gourd sits its own way off its key; the top keys carry no membrane |
| Centred strike parity | the partial at 4 f0 | the key's own second mode | A balafon's second mode is not at 4:1 |
| Air ablation (`CavityPhysicsTest`) | struck at 0.25 | at 0.35 | A balafon key's fundamental node lies between 0.20 and 0.25 |
| Rate and brightness comparisons (`TouchResponseTest`) | one stroke | four strokes' mean; brightness only above −60 dB | The heads' fibre noise is drawn anew at every rate |
| Attack peak bound | < 40 | < 64 | The thinned end of a balafon key under a hard stick |
| Collision count across rates | 6 % | 8 % (energy unchanged at 0.2 dB) | Tiny extra grazes of the balafon's top key at 88.2 kHz |
| Engine parameters (wrapper) | 35 fields | 37 | Carving, Tension |
| Carving and Tension's panel (`validate_extension`, `validate_panel_geometry`) | rear, bound through the wordmark | front knobs under Resonance and Character | Asked for on the front; the wordmark keeps the eight rear controls |
| Rear knob rows (`validate_panel_geometry`) | one setup row below the CV inputs | that row, plus one tuning row in the audio band right of every output | The outputs pack to the left with Tune, Detune and Scale beside them |

## Limits

- No real instrument was recorded; nothing here is a listening test.
- The bass bloom peaks 40 % later than the references at G3–C4 while its height
  matches them.
- A muffled tube's Q (35) and the dead stroke's hold force (5 N) and grip are
  inferences: no measurement of either exists.
- The bow's onset is faster than Essl & Cook's machine on aluminium (seconds);
  wood loses more, and the hair lands and draws in 40 ms.
- The hole cover's palm is a disk; real hands leak around the fingers.
- The gyil's 0.75 f0 line, Scala files and MIDI tuning are not supported (see
  above).
