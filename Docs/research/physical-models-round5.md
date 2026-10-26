# Fifth physical-model pass — 1.4.0b1

> 1.4.0b1 combines this pass with the 1.3.0f2–f5 line developed alongside it
> (Buzz range, Strike Variance curve, mounted kalimba shapes, membrane
> material, the feedback update's bank rules): see
> [combined models](combined-models-1.4.0b1.md). Where they differ, that page
> describes the engine.

This pass follows an evaluation of the four models against measurements of
real instruments. It keeps every stored property, range and automation
identity, adds two rear controls (**Scale**, **Note Off**) with new MIDI ids 289
and 290, gives the mod wheel a new job, and raises the version to 1.4.0b1.
Existing patches load with their stored values and sound different by intent.
The [research notes](physical-model-round5-references.md) list what each source
supports; [calibration](calibration.md) describes the tools used against the
reference recordings.

The [sixth pass](physical-models-round6.md) revised several findings below on
further research: the tube's Q and its tuning against the bar (now wall and
mouth losses, and tubes set above their bars), rosewood η0 (0.0036), the
Guatemalan bars (now tuned in their fundamental only, not with a 9.8 third
partial), the balafon keys' overtone spread, Note Off (a third mode, Dead) and
the Scale choices (two measured keyboards, per-key scatter). Where the two
disagree, the sixth pass describes the engine.

No recording of a real instrument was made or used for this pass apart from the
repository's MP3-derived soundfont notes (`Demos/Reference`). The numbers below
compare the model with published measurements and with those notes; they are
not a listening test.

## What changed, per instrument

| Instrument | Changes |
| --- | --- |
| Imperial Rosewood | Bars carved per key by a Timoshenko finite-element model (1:4:10 to G♯3, 1:4 above, fundamental only in the top octave); wood loss from Hill's decay times; physical mallet head; tube coupled just below critical, with the bar tuned against its tube; rolls on the mod wheel |
| Mayan Padauk | Hormigo bars with a 9.8 third partial in the bass; cedar boxes; the charleo buzzes at mezzo-forte in the Guatemalan patches without re-levelling the note |
| Balafon Ancestral | Vène keys that are not overtone tuned (first overtone fixed per key, 2.2–3.6 × f1); two or three mirlitons per gourd with stroke-to-stroke thresholds; the Bala 7-equal, Timbila, pentatonic and Sambla tunings |
| Kalimba Artisan | One shared box (Helmholtz air mode and plate modes) driven by all tines' bridge force; the mod wheel covers its sound holes; the thumb stops the old ring before a re-pluck; tines ring along with their physical neighbours |
| All | Note Off (Ring/Damp); loudness-based voice stealing; rebuilt Wood and Chamber rooms; direct-output trim −18.4 dB; calibration and listening-test tools |

## Bars from a finite-element model

`Tests/generate_bar_modes.py` designs each bar the way a maker does: it removes
wood from a symmetric undercut until the tuning targets are met, then solves the
free–free Timoshenko beam (shear and rotary inertia matter for the short, thick
treble bars) for 16 bending modes, St-Venant torsion for two torsional modes and
lateral bending for one. `DSP/BarModes.h` stores anchors every three semitones
from C1 to C8: ratios, modal-mass fractions, half-bar mode shapes, torsional and
lateral ratios and the bar's dimensions and mass. `--check` (run by the suite)
regenerates the table and compares it byte for byte.

| Family | Tuning target | Generated |
| --- | --- | --- |
| Rosewood | 1:4:10 to G♯3, 1:4 to F♯5, fundamental only above | 10:1 held to A3, 4:1 to C5 (3.64 at F♯5, 2.27 at C8) |
| Hormigo | 1:4:9.8 in the bass, 1:4 in the middle | 9.8–9.9 to D♯3, 4:1 to F♯4 (3.80 at C5) |
| Balafon | one fixed first-overtone target per key in 2.2–3.7 | 2.2–3.6 (2.55–3.42 from C3 to C6) |

Against Bork et al.'s measured rosewood C3 bar (`--bork`), the generator puts V2
to V4, T1 and L1 within 0.7 %, T2 at −4.5 % and V6–V7 at −6.2 %; V5 is 14.9 %
high, outside the ±12 % the plan asked for.

Modal states are now mass-normalized displacements, so the mallet sees the
bar's real driving-point response and the bar–air junction exchanges energy in
consistent units.

## Wood loss

Each mode's loss is its wood's loss factor at its own frequency,
η(f) = η0 + η2 (f / 1 kHz)² + a support term below the lowest bars, with
T60 = 2.2 / (η f): rosewood η0 0.0039, hormigo 0.0048, vène 0.0056. The Decay knob
scales it as before. Bare rosewood fundamentals against Hill's YM-5100A:

| | C2 | C3 | C4 | C5 | C6 | C7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hill (derived from η) | 6.7 s | 4.7 s | 2.2 s | 1.1 s | 0.54 s | 0.18 s |
| Model | 6.76 s | 4.03 s | 2.02 s | 1.01 s | 0.48 s | 0.16 s |

The 4:1 partial rings 0.22–0.26 and the 10:1 partial 0.05–0.09 of the bar's own
fundamental (Hill: about 0.25 and 0.10–0.12 at C2–C3). Kalimba tines ring 5.3 s at
C4, 2.4–4.2 s from E4 to B4 (median 2.7 s, the note on the box resonance
shortest) and 0.39 s at E6.

## The mallet head

`DSP/MalletContact.h` is a mass on a nonlinear spring, F = K dᵖ (1 + λ ḋ) plus a
harder core for yarn and cord, solved every sample against the bar's surface:
the voice predicts the surface without this sample's force, and the force moves
it by the bar's compliance within the same sample. The trapezoidal rule keeps a
lossless contact's energy and impulse independent of the rate; a safeguarded
Newton solve on the compression converges in a few iterations.

| Striker | Head | Law | Rigid wall, 1 m/s |
| --- | ---: | --- | ---: |
| Soft yarn | 30 g | p 2.8 with core | 2.40 ms |
| Medium cord | 28 g | p 2.5 with core | 1.50 ms |
| Hard rubber | 26 g | p 1.5 (Chaigne & Doutaut) | 0.60 ms |
| Wood baton | 24 g | p 1.5 (Chaigne & Doutaut) | 0.18 ms |

`MalletContactTest` checks these times, elastic rebound (impulse 2mv within 2 %),
the Hertz velocity law, hysteresis, rate invariance (1 %) and passivity against a
ringing mode. A quiet restrike (velocity 8) on a ringing C3 or C4 takes 1.1 dB
from its ring, as a light mallet on a heavy bar should (`Round5ResponseTest`).

## Resonators

Only the lowest air mode of each tube, box or gourd takes part: the upper ones
sit between the bar's symmetric partials, and measured through the junction they
added nothing above −50 dB. The bar–air junction is an energy-preserving
rotation (worst relative energy error 4 × 10⁻¹¹), and only symmetric bar modes
move air through the mouth.

Rosewood's tubes (Q 45 at C4) are coupled just below critical coupling: the
bar's energy flows into the air and out of the mouth in one bloom without a
beating twin. With its tube, C2's fundamental rings 2.8 s against 6.8 s bare
(0.42; Hill measured 3.7 against 6.8 s, 0.54). Tubes sit −1 to −9 cents from their bars through
C5 (the bar over the mouth loads the tube) and −33 cents at C7. Malletech's
makers sharpen their top-octave tubes for volume; the model lowers them for
length, a voicing choice.

**The key stays on pitch.** The coupled pair splits the fundamental. Every
strike solves for the bar frequency whose bar-like coupled mode, as heard through
a Hann window from 0.15 s, lands on the key (in the bass, where a tube within its
coupling of the bar makes that mode jump across the note, it refines the heard
line directly). Across Coupling 0–1 and Tune −50…+50 cents from C2 to C6 on all
three wooden models, the heard pitch is within 0.2 cent of the key. The first 0.15 s read up to 12 cents flat while the air
answers, as the reference notes do (−7 to −10 cents at G3–E4).

Bloom, default patch voicing (dry close mic), against the reference notes:

| | G3 | C4 | E4 | G4 | C5 | E5 | C6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Reference peak | 104 ms | 76 ms | 74 ms | 54 ms | 37 ms | 22 ms | 14 ms |
| Model peak | 125 ms | 82 ms | 67 ms | 56 ms | 40 ms | 31 ms | 16 ms |
| Reference rise | 8.8 dB | 5.7 dB | 3.8 dB | 3.2 dB | 1.2 dB | 0.1 dB | 0.6 dB |
| Model rise | 3.6 dB | 3.6 dB | 3.2 dB | 2.6 dB | 1.3 dB | 0.5 dB | 0.1 dB |

The timing follows the references; the model's bloom rises less in the bass
(the default patch strikes near the centre, where the bar's own attack is
strongest). The references' decay times are not consistent from note to note
(E4 0.49 s, G4 1.03 s), so the decay is set from Hill's measurements instead.

## Buzzing membranes

The balafon has two or three mirlitons per gourd; each stroke draws their
thresholds anew. The buzz readout is the membranes' motion beyond what a slack
shadow membrane would do, high-passed (fourth order, 1.5 f0) and notched at f0:
a clipped membrane passes less of the fundamental than its shadow, and that flow
stays in the resonator rather than radiating as an out-of-phase fundamental. The
air energy drawn by each slap was reduced four- to ninefold: the buzz drained
up to 90 % of the fundamental's energy while radiating about 1 %.

| Fundamental with vs without Buzz (C3–C5, 0.05–0.4 s) | Worst change |
| --- | ---: |
| Buzz ≤ 0.75 (the factory range), velocity ≤ 0.7 | 1.43 dB |
| Buzz 1 or velocity 1 | 3.08 dB |

A loose membrane on a loud stroke clamps the resonator near its threshold,
turning the excess into buzz, so forte strokes at full Buzz compress by up to
3 dB. The charleo is heard at mezzo-forte: its buzz layer sits at −31 dB (Buzz
0.35) and −16 dB (Buzz 0.75) relative to the note. The Balafon remains the
strongest buzzing instrument at every register, velocity and setting tested.

## The Kalimba's box

`DSP/KalimbaBody.h` is one body for every tine, driven by the summed bridge
force: a Helmholtz air mode at 400 Hz (a 0.8 L 17-key box; Hugh Tracey's smaller
boxes sit at 440–520 Hz), three plate modes and the board's broad radiation. The
holes radiate opposite to the board's own push, as in a bass-reflex box: below the
air resonance they take 1–2 dB from the board's sound, at it they add 5.5 dB
(Body 0.5), above it they reinforce the board. Notes near the resonance are the loudest (G4 and A4) and die a little
sooner. Resonator Tune and Coupling move and scale the air mode.

The mod wheel covers the holes (12 ms smoothing): the air resonance falls by up
to 38 % and radiates less. With Body at 0.5, G4 drops 4.3 dB and A4 3.9 dB, C4
rises 3.5 dB and E4 2.4 dB, and the pitch moves by less than 10⁻⁵ cent. Moving
the hand over a ringing note does not click.

A re-pluck starts with the thumb on the tine: the old ring loses 26 dB. The tine
rocks on the bridge rod, adding a line at 2 f1. The sympathetic tines are the
17 tines of a C kalimba, each hearing the others' bridge force by distance on
the bridge (neighbours a third apart ring most).

## Sympathetic keys

Every receiver is a real key with its own loss (Q = 1/η), tuned with Tune, bend
and Scale. Bars C2–C7 are driven by the summed force at their cords, so a key
rings when a partial of the played note falls on it. Transport stop damps them
with the struck keys.

## Playing features

**Scale** (`DSP/Tunings.h`): the degrees of each tuning sit on the keys a player
reaches for, and the other keys fall between their neighbours so runs stay
monotonic; octaves repeat. A traditional-tuning key sounds exactly like the
equal-tempered key retuned by its offset (checked to −120 dB), and the intervals
are heard as tuned on every instrument (C4–E4 within 0.005 cent).

| Scale | Pitch classes (cents above C) |
| --- | --- |
| Bala 7-equal | white keys at multiples of 171.4 |
| Timbila | white keys at 0, 157.5, 324.8, 498.0, 655.6, 834.4, 1026.9 (Tracey's Hz) |
| Pentatonic 5-equal | C D E G A at 0, 240, 480, 720, 960 |
| Sambla | C 0, D♯ 295, E 400, G 700, A 900 |

**Note Off**: Ring is bit-identical to no note-off. Damp stops the released key
with a per-model hand rate: 40–59 dB down 0.2–0.3 s later, with no step larger
than the ringing note's own.

**Rolls**: with the mod wheel up, every held key on a mallet instrument is
struck again 6 (bottom) to 20 (top) times a second by two alternating mallets,
±3.5 % of the bar either side of the set position, the second 8 % softer, with
7 % timing and 5 % force scatter. At 13 strokes a second a held C4 sustains 0 to
+4 dB above its first stroke. Releasing the key, lowering the wheel below 2 % or
stopping the transport ends the roll; a held key with the wheel down lets the
engine go idle, and raising the wheel rolls it again. A rolled chord restrikes
its own voices.

## Factory patches

Seven new patches (18 in all), each storing every property:

| Patch | Model | Shows |
| --- | --- | --- |
| Mandinka Bala Heptatonic | Balafon | Bala 7-equal, mirlitons (Buzz 0.60) |
| Chopi Timbila Ensemble | Balafon | Timbila tuning (Buzz 0.45) |
| Sambla Talking Balafon | Balafon | Sambla tuning (Buzz 0.70) |
| Equipentatonic Log Keys | Balafon | 5-equal pentatonic, wood sticks, no membranes |
| Guatemalan Charleo | Padauk | The charleo at Buzz 0.75 |
| Rosewood Dampened Etude | Rosewood | Note Off: Damp |
| Kalimba Muted Thumbs | Kalimba | Note Off: Damp |

Every factory patch stores Scale and Note Off, and VEL - Position at 0 %. Older
patches changed where the new physics asked for it:

| Patch | Change | Why |
| --- | --- | --- |
| Concert Grand Rosewood (default) | Hardness 0.40 → 0.55, Position 0.50 → 0.47 | Voiced with the physical head to the reference notes' attack brightness; off the exact centre the 4:1 partial is not cancelled |
| Ambient Cathedral Bloom | Position 0.50 → 0.46 | The same |
| Chiapas Folk Fiesta, Mayan Dance Padauk | Buzz 0.10 → 0.35, 0.05 → 0.40 | The charleo belongs to the Guatemalan sound |
| Direct Piezo Mallet | Hardness 0.70 → 0.40, Volume 0.87 → 0.93 | A hard wood head's attack clipped the direct outputs |
| Soft Yarn Nocturne | Position 0.48 → 0.47, Volume 0.85 → 0.80 | Loudness window (`AllPatchRenderTest`) |
| Kalimba Thumb Lullaby | Volume 0.85 → 0.97 | Its low notes now sit below the box's air resonance |

## Other changes

- **Voice stealing** compares what each voice sounds like now (a 30 ms loudness
  tracker), so bass and treble voices compete by what is heard.
- **Rooms**: Wood and Chamber rebuilt as dense, band-limited feedback-delay
  networks (Wood with first-order wall reflections); see
  [character and rooms](character-and-rooms.md).
- **Direct outputs** are trimmed by −18.4 dB (was −6 dB): a wood baton's 0.2 ms
  contact rings every bending mode in phase, and its attack peaks some 20 dB
  above the ring it leaves.
- **Velocity → Position** defaults to 0 %.
- **Demos**: every render is regenerated from this engine; six original idiom
  studies play rolls, the hole cover, the Bala and pentatonic tunings and Note
  Off. Demo presets keep Buzz at 0 %. The AAC encoder now runs without temporal
  noise shaping, intensity stereo and noise substitution, which made FFmpeg 8
  overshoot hard-mallet attacks by up to 8 dB.

## CPU

Native clang -O3 on an Apple arm64 machine, 48 kHz, 64-frame blocks, 16 voices,
default 2x (`Demos/performance_review.cpp`, thread CPU per block, paired runs
against the 1.3.0f1 sources):

| | 1.3.0f1 | 1.4.0b1 |
| --- | ---: | ---: |
| Rosewood / Padauk, median | 176 µs | 192 µs |
| Balafon, median | 176 µs | 218 µs |
| Kalimba, median | 152 µs | 142 µs |
| 16-note restrike burst every 32 blocks, p99, wooden models | 461–511 µs | 541–562 µs |
| The same burst, Kalimba | 365 µs | 212 µs |
| 16 fresh notes over 16 ringing ones: note-ons plus the next block, wooden models | 560–575 µs | 0.91–1.05 ms |

A mallet in contact solves its head every sample (two to three Newton steps),
and a fresh strike tunes its bar against its resonator and projects each mode's
radiation: 20–30 µs per fresh strike, 7–8 µs per restrike (which keeps its
projection). The first version of this pass took 1.75 ms for the restrike burst;
a Newton start from the last force, exact-root and bracket-end handling,
square-root power laws, one reciprocal per window term, per-strike path tables
and projection reuse brought it to 0.56 ms. No deadline (1.33 ms) was missed in
the burst test; a fresh 16-note chord over 16 ringing notes comes within 0.3 ms
of a 64-frame block's deadline, so at a 64-frame host buffer on a slower machine
it could drop out (larger buffers spread the spike over several blocks). These
are not Rack Extension toolchain or Reason scheduler measurements.

## Validation

`bash Tests/run_all_tests.sh` passes all steps, including the wrapper host test
and the all-patch render at 44.1, 48 and 96 kHz. `test_dsp` and every realism
contract also pass under AddressSanitizer and UndefinedBehaviorSanitizer. New or
rewritten contracts:

- `Round5ResponseTest`: tunings, Note Off, rolls, the hole cover, pitch across
  the resonator controls (C2–C6), Buzz levels, quiet restrike, re-pluck, Hill
  and Holdaway decay.
- `RoomQualityTest`: echo density, roughness, band T60s, rate invariance and
  levels of the three rooms.
- `MalletContactTest`, `CavityPhysicsTest` and `ModalRadiationTest`: rewritten
  for the physical head, the one-mode junction and the carved shapes.
- `BarModesTest`: the carved-bar tables as the voice reads them (ordered,
  continuous modes; shape parity and node counts; the tuning targets).
- `generate_bar_modes.py --check` and `test_calibration_tools.py`.

Contracts whose old assumption this pass removes were changed, with these
reasons:

| Contract | Before | Now | Why |
| --- | --- | --- | --- |
| Onset pitch (`test_dsp`) | ±2 cents in the first 0.3 s (new strike ±1) | ±12 (new strike ±8); settled ±1 unchanged | Near-critical coupling: the air-like line is strong while the tube blooms, as in the reference notes |
| Settled pitch windows | from 0 s | from 0.15 s (C8: 5–45 ms) | A tuner reads after the bloom; C8 rings for tens of milliseconds |
| Treble level (`test_dsp`) | C7 within 15 dB of C4 | within 18 dB | Hill's decay (C7 0.18 s) and a cord mallet's 1.5 ms contact leave the top octave quieter |
| Tine tip vs inner fundamental | > 2× | > 1.4× | The thumb now works the last fifth of the tine, not 35 % |
| Kalimba artifact level | read at the close mic | read at the pickup | A tine sounds through its bridge; the close mic has only its weak direct radiation |
| Artifact level across rates | < 1 dB | < 1.5 dB | A rattling mount re-strikes every half cycle of a C7 bar; which cycles it catches can shift by a sample |
| Collision onset | within 2 samples | within one C7 period | The knock follows a swing of the bar past its clearance, calibrated once the mallet has left |
| Membrane soft/quiet linearity | 0.5 dB | 1 dB | Thresholds are drawn anew each stroke |
| Buzz in the pickup | whole difference < −30 dB | the part above 2 kHz < −40 dB | The drain shortens the bar's ring, which the pickup rightly shows |
| Sympathetic bank edge | adjacent notes within 3 dB | within 10 dB | Real keys answer only coinciding partials |
| Sympathetic static input | < −70 dB | no held output (a step leaves real keys ringing) | Receivers are keys with Q = 1/η |
| Torsion at the centre | T1 = 0 | T1 ≤ 0.1 T2 | A stroke lands within half a millimetre of the centre even at zero Variance |
| 10:1 decay ratio | > 0.08 | > 0.04 (against the bare bar) | Hill's constant loss factor with its rising term above 2 kHz |
| Restrike node, position studies | node at 0.2242 | each carved bar's own node | Undercut bars have their nodes nearer the ends |
| Transport stop and pitch bend | room on | room off | The new Wood's 4 ms reflections interfere with the dry sound; a room keeps what was played |
| Direct-out trim | 0.5 | 0.12 | −18.4 dB headroom |
| Voice steal (wrapper) | output difference | the released voice's state | Loudness-based stealing |
| Mod wheel (wrapper) | adds 0.35 to Hardness | reaches the engine's wheel; Hardness untouched | Rolls and the hole cover |
| Attack peak bound (`TouchResponseTest`) | < 2 | < 40 | A wood head's 0.2 ms contact rings every bending mode in phase: its spike reaches about 10× a yarn stroke's while the ring stays within 2 dB |
| Rate and mic-mix comparisons (`TouchResponseTest`) | every case | rings above −60 dB | A pianissimo yarn stroke barely moves a treble bar |
| Collision test rates | 44.1–768 kHz | 88.2–768 kHz | Voices always run at least 2x oversampled |
| Kalimba repeat variation (`test_dsp`) | read in the ring | read in the attack | At Decay 0.1 a tine's ring is its bare fundamental within 40 ms |
| Pitch after an oversampling switch | from 50 ms | from 150 ms | After the bloom |

## Limits

- No real instrument was recorded. Calibration used published measurements
  and the soundfont's MP3-derived notes, whose own decay times are inconsistent.
- The Guatemalan overtone tuning (the 9.8 third partial) is an assumption; no
  measurement was found. The charleo's threshold, formant and level are voiced.
- Balafon keys land at 2.2–3.6 × f1; the rarer keys measured near 1.9–2.2× and
  above 4× are not produced. No balafon decay measurement exists; its loss is
  the wood's.
- The generator's V5 misses Bork's measurement by 14.9 %.
- A balafon key's length and carving are solved together for its overtone, so
  neighbouring keys' lengths and masses differ by up to about 20 % either way,
  likely more irregular than a real instrument's.
- The rosewood's top-octave tubes are lowered for length where makers sharpen
  them for volume.
- Membrane formants, gaps, stiffness and drain, the box's plate modes and the
  hole radiation's strength are voiced; the hole cover's depth is not measured.
- Mallet laws for yarn and cord have no published measurement.
- No listening test was done. `Demos/build_listening_test.py` builds the blind
  ABX and MUSHRA pages for one.
