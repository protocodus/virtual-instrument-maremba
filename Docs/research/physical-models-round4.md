# Fourth physical-model pass

This pass is compared with the tree at `181d5a3`, frozen before editing in
`/tmp/maremba-realism-baseline/DSP` and copied into
`Release/validation/physical-models-round4-20260922/sources/before`. It keeps the
1.3.0b4 development identity, every control, parameter range and default, and
the default 2x quality. No control was added. This is an intentional sound
update: existing patches keep their stored values but sound different.

Each change starts from a measurement or a published observation that the
previous model contradicted. The [research notes](physical-model-round4-references.md)
list the sources and what each one supports.

## What changed, per instrument

| Instrument | Improvements |
| --- | --- |
| Imperial Rosewood | Tube radiates the low fundamental, which blooms; measured wood damping for upper partials; torsional modes from off-axis strokes; per-key maker's scatter (tight: concert tuning) |
| Mayan Padauk | Box radiates the low fundamental; the Mesoamerican **charleo** membrane as native Buzz; measured damping; torsional modes; per-key scatter of bars and boxes |
| Balafon Ancestral | Gourd radiates the low fundamental; the **mirliton** now snaps taut and costs the gourd energy; measured damping; torsional modes; per-key scatter with gourds up to ±30 cents from their bars |
| Kalimba Artisan | Overtone ratios of a **mounted** tine (yielding bridge, pinning bar) matched to measurements; the bridge carries the overtone's energy away, so the "tink" dies quickly; Buzz rattles **board buzzers**; per-tine scatter |

## The resonator, not the bar, radiates the low fundamental

**What was wrong.** At the default Coupling the resonator's contribution to the
fundamental was 23–33 dB below the bar's own radiation in the Close microphone,
so tubes, boxes and gourds were practically inaudible. Raising Coupling only
damped the bar and made notes quieter. In the repository's recorded marimba
references (`Demos/Reference`, removed 2026-10-31), the fundamental rises 8–13 dB and peaks 50–160 ms
after the stroke (C3 160 ms, C4 80 ms, G4 50 ms), while the 4:1 and 10:1 partials
peak at once. The model's fundamental peaked immediately.

**Physics.** A bar's upper face and lower face move air in opposite
directions, and the air flows round its edges: at low frequency a bar is an
inefficient, short-circuited dipole. The resonator mouth is a monopole.
Doutaut, Matignon and Chaigne model exactly this pair and measure the
directivity changing from dipole to monopole within 10–50 ms. For a xylophone
A4 bar with a wide tube, they also report the bar's components about 20 dB
below the tube's.

**Implementation.** Each bar mode's direct radiation is scaled by a dipole
efficiency `kd / sqrt(1 + (kd)^2)`, with `d` an edge path of 1.6 bar widths.
It is normalized to unity at C4's tuned 4:1 partial, so the voiced upper-mode
balance near 1 kHz is kept. The cavity readout gains the mouth's monopole
radiation. Rosewood's tube Q rises from 13 to 20, which gives the slower bloom
of the recordings. The far microphone's cavity weight is lowered so that the
direct Far outputs stay near their previous level. Kalimba readouts are
unchanged: its body, not its tine, radiates.

| Rosewood, default Coupling | C2 | C3 | C4 | C5 | C6 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Tube over bar at the fundamental | +18.1 dB | +13.6 dB | +8.1 dB | +3.4 dB | −2.4 dB |
| Fundamental peak after the stroke | 218 ms | 112 ms | 60 ms | 30 ms | 16 ms |

At C3, Coupling 0.7 raises the fundamental 12.8 dB (Rosewood), 9.7 dB
(Padauk) and 11.3 dB (Balafon) above bare bars. Coupling is now what it is on
a real instrument: more resonator, louder and shorter low notes. The bloom's
field is in quadrature with the bar's, so while it takes over, the summed
phase drifts by up to about 1.5 cents' worth in the first 0.3 s. The settled
pitch stays within ±1 cent.

## Upper partials ring as long as measured wood allows

**What was wrong.** The 4:1 partial rang for 0.14–0.18 and the 10:1 partial for
0.04–0.05 of the fundamental's T60. Measurements put them near 0.25–0.6 and
0.1–0.2: Chaigne & Doutaut's padauk bar gives 0.6 and 0.19, Hill's rosewood
bars 0.25 and 0.1 (a constant loss factor), and the references 0.23–0.41 and
0.16–0.22.

**Implementation.** The per-mode law keeps its form (a loss ∝ f plus an f² term)
with the f² term cut by a factor of 6–9: a near-constant loss factor, as
measured. Now Rosewood rings its 4:1 and 10:1 partials for 0.32 and 0.13 of the
fundamental, Padauk 0.30 and 0.11, Balafon 0.26 and 0.10. With a hard mallet off
centre, Rosewood C4's 4:1 partial now sits at −21 dB in the attack (reference
−19 dB; before −26 dB).

## Torsional modes

A bar also twists. Its centre line is a nodal line of twist, so only the
off-axis part of a contact drives it, and a C3 rosewood bar has T1 = 2.48 and
T2 = 11.1 × f1 (Bork et al.). Across a five-octave marimba, T1 falls from about
1.9 × f1 in the largest bars to about 1.2 × f1 in the smallest (Rossing, Yoo &
Morrison). Two torsional modes now follow that law: 2.08 at C2, 2.48 at C3,
falling to 1.25 at C7, with T2 = 4.46 × T1. Each stroke lands off-axis by a
random amount (spread 0.25, 0.30 and 0.35 of the half width for Rosewood,
Padauk and Balafon). Variance widens it and a glancing stroke lands near an
edge. The twist shapes `cos(nπx)` put T1's node, but T2's antinode, at the
centre of the length. Opposite edges cancel much of the radiation, and twisting
moves no net air, so the modes never couple to the resonator. Across four hard
C3 strokes, T2 measures −36 to −64 dB relative to the fundamental in the first
80 ms. It differs every stroke, like the inharmonic lines in the reference
recordings (−20 to −40 dB).

## Each key is its own

A fixed, deterministic scatter per key touches only what makers do not tune
exactly: untuned upper partials, torsion, resonator alignment and loss. The
fundamental stays exact.

| | Tuned partials | Untuned partials | Resonator | Loss |
| --- | ---: | ---: | ---: | ---: |
| Rosewood | ±0.3 % | ±1.2 % | ±1 cent | ±4 % |
| Padauk | ±0.6 % | ±2 % | ±10 cents | ±8 % |
| Balafon | ±2.5 % | ±4 % | ±30 cents | ±12 % |
| Kalimba | ±1.5 % | ±1.5 % | — | ±6 % |

Concert resonators are tuned closely to their bars, so the Rosewood scatter
stays at ±1 cent.

## Buzzing membranes: mirliton and charleo

**What was wrong.** The previous membrane was a hardening spring behind a
dead zone. Its contribution was centred at 150–590 Hz, on the note's own
fundamental and second harmonic, which is a mild distortion rather than a buzz.
It never shortened a note. The gyil's mirlitons buzz only when a bar is played
with enough force, and its gourds add components near 1.2 and 1.8 kHz
(Godlovitch et al.). Mesoamerican marimbas carry the same device, the
*charleo*: a pig-intestine membrane over a hole ringed with wax at the bottom
of each resonator.

**Implementation** (`DSP/SlackMembrane.h`). A slack membrane follows the air
pressure until it reaches one of its two limits. The limits differ, because a
membrane is fixed bulging. Past a limit a stiff one-sided spring snaps it back,
and it rings at its taut resonance, a fixed formant: 1.15 kHz for the Balafon's
spider silk, 0.65 kHz for the Padauk's heavier gut, 0.9 kHz for Rosewood's
add-on membrane. A slap loses energy in proportion to the overshoot, and that
loss is taken from the air modes. This makes the membrane a nonlinear energy
sink that acts only above the buzz threshold. A discrete-gradient update keeps
the membrane's own energy balance exact.

Most of the slack motion is already part of the resonator's radiation. A
shadow membrane that never goes taut is driven identically, and the buzz is
the difference plus a tenth of the slack motion. Soft strokes therefore
colour the tone without re-levelling the note. Buzz loosens the membrane
(smaller gaps, stronger drive). Treble resonators carry smaller membranes that
reach their limits at lower pressure.

| C4, Buzz 1, forte | Buzz above 800 Hz | Centroid |
| --- | ---: | ---: |
| Balafon mirliton | −9.8 dB re note | 1.39 kHz |
| Padauk charleo | −23.3 dB re note | 0.66 kHz |

At Buzz 0.5 the membranes are linear on soft strokes (the same level relative
to the note at velocity 0.2 and 0.3) and cross the threshold on forte strokes:
27 dB more for the Balafon. The buzz fades while the note rings on. The
Balafon remains the strongest buzzing instrument at every register, velocity
and setting tested.

## The mounted kalimba tine

**What was wrong.** The round-3 tine sat on a rigid bridge with a clamped
rear span. That gives a first overtone at 6.42 × f1, above an ideal cantilever's
6.27. Chapman measured 5.86, 5.63, 5.76, 5.61 and 5.30 on five tines, falling
toward the treble. McNeil and Mitran report mbira overtones near 5× and 14×. A
rigid bridge with a clamped bar cannot go below 6.27, whatever its rear span.

**Physics** (`Tests/generate_kalimba_mount.py`). The pressure bar pins the tine
with a line contact, and the bridge yields under the tine's reaction. Every
tine shares one bridge, so the bridge's stiffness relative to a tine,
`k a³/EI`, scales with the cube of the tine's length. Short treble tines see a
softer support, and that lowers their overtones more than their fundamental.
One fitted constant reproduces all five of Chapman's tines with an RMS error
of 0.08 (0.10 through the run-time table), and places the second overtone at
about 12–16 × f1. A consistent-mass Hermite
beam is solved offline at two resolutions for 12 notes from C3 to A5. Outside
that range, where the law would extrapolate, the end values are held.

The same bridge motion carries energy into the board, which is how a kalimba
radiates at all. Half the fundamental's loss leaves that way, and every mode's
share scales with its bridge motion. The first overtone therefore decays at
0.17–0.24 of the fundamental's T60 (previously 0.37–0.43), and its sound is the
short "sproing" Chapman describes.

| Rendered, thumbnail | G3 | C4 | G4 | C5 | G5 | C6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First overtone, before | 6.43 | 6.42 | 6.43 | 6.42 | 6.42 | 6.42 |
| First overtone, now | 5.97 | 5.98 | 5.72 | 5.52 | 5.15 | 5.05 |

Contact, radiation and pickup shapes keep the round-3 rigid-bridge solution.
A yielding bridge also changes the shapes, which is not modelled, so
frequencies and shapes no longer come from one boundary problem.

## Kalimba buzzers

A 17-key kalimba has no resonator membrane. The previous model still gave it a
fallback membrane. Mbira-family instruments buzz with loose metal rings,
bottle caps or shells on the board or resonator (*machachara*). Kalimba Buzz
now loosens such buzzers (`DSP/BuzzerRattle.h`). A buzzer rests while the
board pushes it and lifts off when the board accelerates away faster than its
preload. It lands with an impact that rings its thin-metal modes (3.1 and
5.6 kHz), and the board-mounted pickup hears the knock. Flight is integrated
in physical time, so the level is the same at 48 and 96 kHz (0.08 dB apart).

At the default Buzz (0.15) the default striker never rattles. On the hardest
treble strokes (G5–C7, velocity 0.9–1) a thumbnail gives −41 to −73 dB and a
pick −28 to −38 dB relative to the note. At full Buzz a forte C5 rattles at
−27 dB. Buzz 0 removes the buzzers outright.

## Other corrections found on the way

- Voice fades (steal, polyphony, quality changes) use a smoothstep instead of
  a linear ramp. The louder low fundamentals made the ramp's corners audible.
- A restrike that changes Coupling now moves the air exchange over the same
  5 ms as the air readout, not only the readout. The first-sample step fell
  from about 1 % to 0.4 % of the ring.

## Validation

All SDK-free steps and all DSP contracts pass on Linux (clang 18): metadata,
patches, panel geometry, `test_dsp`, the touch and realism contracts, the
demo renderer, and the generator checks. `Tests/Round4ResponseTest.cpp` adds
contracts for every mechanism above. `test_dsp` and every contract also pass
under ASan + UBSan (GCC 13).
Steps 7–8 (wrapper host and all-patch render) need the Jukebox SDK and were
not run here.

Six older contracts encoded assumptions this pass removes. They were rewritten,
not loosened:

| Contract | Old assumption | Now |
| --- | --- | --- |
| Tuning (±1 cent over the first 0.3 s) | No bloom | Settled pitch ±1 cent; onset ±2 cents |
| Buzz release (released − dry < −60 dB) | Buzz is an additive layer | The layer's own energy falls 60 dB within 20 ms |
| Buzz never reaches the pickup | Buzz is only acoustic | Membranes may change the pickup only indirectly (< −30 dB); kalimba buzzers knock it |
| Other models never buzz more than the Balafon | Every model has a fallback membrane | Also true for charleo and buzzers; inaudible layers (< −60 dB) excepted |
| Steal victim G4 silent | No neighbour energy at G4 | Victim E3; windowed measurement (C3's tube 12th and torsion sit on G4/E4) |
| Kalimba partial balance | Rigid-bridge ratios | Measured at the mounted ratios |

Default-patch loudness over the first second, relative to before (vel 0.8):

| | C2 | C3 | C4 | C5 | C6 | C7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Rosewood | +3.0 dB | +0.2 | +0.5 | +0.9 | +1.0 | +1.7 |
| Padauk | +1.9 | +0.2 | −0.3 | 0.0 | +0.7 | +1.4 |
| Balafon | +1.2 | +1.7 | 0.0 | +0.2 | +0.2 | +2.0 |
| Kalimba | +0.1 | +0.2 | −0.1 | −0.2 | +0.1 | 0.0 |

Direct outputs stay below −4 dBFS on the forte default chords. With a hard
strike at Coupling 100 %, the direct Far tap is 5.7–8.4 dB louder than before,
because the resonators now radiate. Users of that tap with high Coupling
should check their gain staging.

Native CPU on this Linux VM (48 kHz, 64 frames, 16 voices, 2x, thread CPU
median per block): steady playing is unchanged within noise for the wooden
instruments (225–242 µs before and after). Kalimba rose from 201–205 to
216–220 µs, mostly the per-sample buzzer check at the default Buzz. Under
16-note restrike bursts, the wooden instruments rose about 10 % (280 → 307 µs)
and Kalimba 13–29 %. The burst p99 stayed at 0.62–0.93 ms against a 1.33 ms
deadline. These are not Apple M1 figures and do not establish host
scheduling.

The common-gain before/after renders, the player and their measurements are in
`Release/validation/physical-models-round4-20260922` (`Demos/build_physics_review.py`).
In the dry and default mixes the wooden instruments stay within ±1.1 dB of
their previous level while the waveform changes substantially. The Kalimba
differs by −30 dB, because its change lives in the brief attack overtone.

## Limits

- Dipole and monopole radiation are relative scalings calibrated against
  published directivity and the reference recordings, not an absolute SPL
  model. The balance falls a little faster toward the treble than the
  references (C5 +3.4 dB against about +8 dB at G4 in the recordings).
- Torsional drive levels, the off-axis spread and the shear-loss factor (0.6
  of a bending mode's T60 at the same frequency) are voiced. Only the
  frequency law is measured.
- Membrane formants, gaps, stiffness and drain are voiced. The gyil study
  supports the 1–2 kHz band and the force threshold, but no one has measured how
  much a mirliton shortens a note. The membrane's reactive detuning of the
  cavity is not modelled.
- The kalimba mount law is fitted to one five-tine instrument and held outside
  C3–A5. Tine shapes remain the rigid-bridge solution. The buzzers' reaction on
  the board is neglected, and no measured buzzer spectra were found.
- No listening test was done. The numbers show where to listen; they do not
  prove that anyone will prefer the result.
