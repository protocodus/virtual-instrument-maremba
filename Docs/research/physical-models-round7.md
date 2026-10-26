# Seventh physical-model pass (1.4.0)

This is a historical report. The unreleased source revision after 1.5.1f1
removes the authored Rosewood rim harmonics and lowers its tube, frame and
sympathetic radiation. Its current bar-led attack supersedes the audible-bloom
targets and `airGain` values below; the mechanical tube coupling and loss law
remain. `RosewoodSpectrumTest` replaces `RimHarmonicsTest` by checking the
absence of the added harmonic layer. See the current [guide](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/guide/user-guide.md).

This pass measured each instrument against recordings or published
measurements and changed what a listener still heard as wrong. Every change was
prototyped, measured before and after, and pinned by a contract. Pitch stays on
the key: the heard fundamental moved by at most 0.06 cent.

The concert marimba was compared with anechoic recordings of a real one: the
University of Iowa Musical Instrument Samples (MIS, 2013), a concert marimba
struck with cord, yarn and rubber mallets at pp, mf and ff, one note per key,
24-bit 96 kHz, 1.5 m. The recordings were measured outside this repository and
none of them is in it or in the product; the fitted numbers are named below and
in `THIRD_PARTY_NOTICES.md`. They are cleaner references than the bundled
soundfont notes in `Demos/Reference/` (MP3-decoded, looped, unknown mallets),
which were compared as well.

## Rosewood

**The tube under its bar.** The free-tube law (Soares, Debut & Antunes) put
bar and tube past critical coupling from C2 to C4. Every bass and low-middle
note bloomed late, beat as a twin "wah" (two fundamental lines of nearly equal
level, 14–16 cents apart) and, from A3 to C5, died 30–55 % too soon. Under a bar
the air that fills and empties the tube is pumped through the gap between the
bar and the rim, and flow separation there adds loss (Ingard & Ising 1967;
Disselhorst & van Wijngaarden 1980). The tube now takes 0.45 of the free-tube
Q in situ (`kTubeGapQ`, `DSP/MarimbaModel.h`) with a coupling of 0.0071 (was
0.0084), which puts bar and tube near or below critical; the mouth readout
(`airGain` 10.4, was 5.78) keeps every Rosewood patch's loudness within about
0.6 dB. Boxes and gourds keep their voiced Q, already their in-situ value.

Dry far microphone, cord striker at mf, recording / 1.4.0b1 / 1.4.0:

| Note | Bloom peak (ms) | Fundamental T60 (s) | Envelope ripple (dB p-p) |
|---|---|---|---|
| C2 | 315 / 430 / 320 | 4.66 / 3.83 / 4.16 | 0.9 / 10.7 / 1.8 |
| A2 | 170 / 260 / 190 | – | 4.2 / 8.2 / 0.9 |
| C3 | 150 / 210 / 160 | 2.86 / 2.31 / 2.56 | 2.4 / 5.4 / 0.6 |
| Gb3 | 110 / 140 / 110 | 2.12 / 1.67 / 1.97 | – |
| A3 | 90 / 120 / 90 | 1.92 / 1.28 / 1.69 | – |
| C4 | 70 / 100 / 70 | 1.38 / 1.00 / 1.42 | 2.2 / 2.8 / 0.0 |
| Eb4 | 30 / 80 / 50 | 1.84 / 0.82 / 1.30 | – |
| C5 | – | 1.00 / 0.63 / 0.86 | – |

The close microphone, 8 cm over the bar, keeps a bloom (4.3 dB at G3, 2.0 dB at
C4; the recording at 1.5 m shows 4.5 and 2.6). C2 keeps 0.68 of its bare ring
(Hill measured 0.54 on his instrument, the Iowa instrument gives 0.68). The
twin line had also made the fitting tool read C3 6.7 cents flat.
This closes the sixth pass's open "bass bloom about 40 % late".

**Odd harmonics from the rim.** In the recordings the keys from B2 to Eb4 carry
lines at 3 f0 and 5 f0: the third 28–55 dB under the fundamental at mf and ff
(median −33), 45–65 dB at pp, the fifth about 11 dB under the third, both dying
2–3 times faster than the fundamental in dB. They sit a median 2 cents from
exactly 3 f0 while the key a twelfth up sits up to 9 cents away, so they are not
the neighbour ringing along: they are the rim's nonlinear loss, whose describing
function radiates odd harmonics (Ingard & Ising; Atig, Dalmont & Gilbert 2004).
The model had none, so its bass sustained as a pure sine. The voice now adds
Re(x + iq)³ and Re(x + iq)⁵ of the tube's own state phasor, normalised at control
rate so each grows as the square of the air's swing, at register levels fitted
to the recordings (`kRimHarmonic3`, `DSP/MarembaVoice.cpp`; 4.5 dB rms error over
C2–E4). They follow the bloom, Decay, pitch bend and the hand. Each is held to
its level at the tube's present swing, so a strike whose first control tick
falls a few samples after the mallet lands (the tube still at rest when its
scale is taken) cannot outgrow it before the next tick; the engine fuzz
contract strikes at every offset against the control grid to keep it so.
Treble keys are unchanged. Cost: about 12 flops per Rosewood tube voice per sample, +5–7 % in the
worst case (16 Rosewood voices, all between C2 and E4); other models 0 %.

## Padauk

**The charleo up to the treble.** Every box of a Guatemalan marimba carries its
charleo, and the melody lives in the treble, but the register compensation
clamped at A4: the buzz stopped above G4 at mf and above C5 even at full Buzz.
Above C4 a tapered box's volume falls faster than its length (p² ∝ E / V) and its
smaller hole takes a smaller membrane, so the charleo's gap now falls as
f0^−2.1 above C4 (was f0^−1.1) and its formant rises as f0^0.5 (was f0^0.15).
Buzz layer re the note, Mayan Dance Padauk at mf: G4 −80 → −20 dB, C5 −109 → −25,
G5 −119 → −33; at ff C6 −128 → −38. Soft strokes (v0.3) stay below −88 dB, C3–C4
are bit for bit as before and the patches' loudness moved by 0.06 dB at most.
The exponents are voicing; no measurement of a treble charleo exists.

## Kalimba

**A continuous pluck.** The thumb's press set the tine's deflection in one
sample, so the bridge force stepped and the board, the pickup, the halo and the
buzzers all heard a one-sample tick: 0.58–1.0 of each note's peak (on a flesh
pluck it was the loudest sample of the note), clicks on every re-pluck, and
buzzers driven only by that step. A tine's deflection, and so its bridge force,
is continuous; a thumb presses over tens of milliseconds, so the board is in
equilibrium under the static load before the release. The jump at the thumb's
touch is now carried as an offset that returns over 40 ms, well below any
tine period (`kPreloadBleedSeconds`). Onset step 0.58–1.0 → 0.002–0.023 of the
peak; re-pluck bursts above 4 kHz −4.7…−20.1 → −27.8…−40.8 dB re the ring; the
Lullaby phrase's peak −8.4 → −16.2 dBFS at the same RMS. With the step gone the
buzzers answer the real ring and tink (`kBuzzerDrive` 0.05 → 0.19): the default
Buzz stays clean for every technique from C4 to C7, and at full Buzz a forte
picked C6 rattles while flesh does not.

**The usual thumb snaps.** Holdaway's box alto shows the first overtone 17–26 dB
under the fundamental 0.1 s after the pluck at F#4 and C5; the natural thumb gave
33–44 dB, because a 0.4 ms release rolls the overtone off (ωT ≈ 5 at 2.1 kHz).
Releases are now 0.2 ms (natural thumb), 0.12 ms (nail) and 0.07 ms (pick); flesh
keeps its slow roll-off. Natural-thumb tink at F#4 −33.4 → −27.4 dB, at C5
−44.2 → −36.6 dB. A velocity-dependent release was tried and rejected: releases
longer than a quarter tine period cut soft notes by up to 15 dB. Even an
instantaneous release leaves C5 about 10 dB under Holdaway; the static-release
pluck model limits it there.

**Stereo from the bridge.** The kalimba played exactly mono (inter-channel
correlation 1.000). Below coincidence a point-driven board radiates its
broadband sound from around the drive point (Cremer & Heckl), so each tine's
board sound now leans toward its side of the 17-key layout (D6 B5 G5 E5 C5 A4 F4
D4 C4 E4 G4 B4 D5 F5 A5 C6 E6, the layout the halo already used), and the tine's
own sound is panned by the same layout; the box resonances and holes stay in
the middle and the mono sum is unchanged (−71 to −105 dB difference). Close-mic
balance: C4 0.0, D4 +0.4, E4 −0.4, D6 +3.5, E6 −3.2 dB. Each voice keeps its own
placement, so a bar still ringing when the model changes to the kalimba stays
where it was.

## Balafon

**The halo rings only keys the instrument has.** In the traditional tunings the
sympathetic halo kept all 61 chromatic receivers, tuned between the scale's
degrees: on the Gyil 36 of them, carrying about 80 % of the halo's energy, are
keys no gyil has. Receivers off the scale's degree keys are now silent (and
skipped, which saves work). Birifor Gyil Buzz halo −27.6 → −34.9 dB re the dry
sound, Mandinka Bala Heptatonic −26.9 → −30.7 dB; the Equal-scale balafon,
including Ancestral Balafon, is bit for bit as before.

## Contracts

- `Tests/TubeInSituTest.cpp`: A2–C4 bloom peak within 0.65–1.35 × the recording,
  T60 within 0.75–1.35 ×, ripple under 3 dB, close-microphone bloom above 3 dB at
  G3 and 1 dB at C4. 1.4.0b1 fails 10 checks.
- `Tests/RimHarmonicsTest.cpp`: C3 at mf has its third harmonic between −40 and
  −25 dB, locked to 3 f0 within 1 cent, the fifth 5–18 dB under it, and 5 dB more
  at ff than at pp; B2–C4 at mf between −48 and −25 dB; F4 and C5 below −55 dB.
- `Tests/CharleoRegisterTest.cpp`: soft strokes clean from C4 to C6, mf buzz from
  C4 to G5 above −40 dB and never above −10 dB up to C5, ff buzz up to C6.
- `Tests/Round7BalafonKalimbaTest.cpp`: no onset step for any technique, click-
  free re-plucks, clean default buzzers, the forte-pick rattle, the natural
  thumb's tink, the bridge stereo layout and the halo on degree keys only.
- Changed by intent: Round6's Hill C2 share window (0.45–0.75, spanning Hill's
  0.54 and the Iowa instrument's 0.68) and its close-microphone bloom thresholds
  (above 3 dB at G3 and 1 dB at C4, the far bloom now pinned by TubeInSituTest);
  the kalimba's organic-floor spectrum bound in test_dsp (1.4 dB, a snappier
  pluck shows the same position scatter more) and its 15 % Variance ratio (1.4 ×);
  PhysicalResponseTest's kalimba pickup-reaction bound (5e-6: the removed step
  made up part of it) and its kalimba buzz layer, now measured with a forte pick
  on C6.

## Measured, not changed

- Eb4–Gb4 on the Rosewood still decay about 30 % shorter than the recording.
- With cord mallets at mf the Rosewood bass attack's 10 : 1 partial is 5–12 dB
  brighter than recorded; the recordings' mallets confound it.
- The balafon rings as long as the concert marimba (C4 T60 1.06 s); Godlovitch
  reports that a gourded gyil decays faster than its bare keys, but no balafon
  decay measurement exists.
- The balafon's two or three mirlitons act as one (the first carries at least
  88 % of the buzz); a 3–5 membrane threshold ladder moved the buzz by 0–2 dB and
  was dropped. The buzz stops within 10–20 ms, from the hard-limit membrane.
- The kalimba's bridge force still uses the rigid-bridge shear jumps
  (`KalimbaBridgeShearJump`) while contact and radiation use the yielding-bridge
  shapes. This remaining mismatch is corrected in the subsequent source update
  described below.

## Subsequent source update: Kalimba bridge reaction

The Kalimba now drives its shared box, pickup, buzzers and sympathetic keys
with the signed reaction of its yielding mount. The existing FEM's bridge boundary is
`phi'''(0+) - phi'''(0-) = -kappa * phi(0)`: a mode that barely moves the
bridge must barely drive the board, even if its free tip moves strongly.
Previously the reaction came from a different, rigid support, so those nodes
and some upper-mode polarities were wrong. The derivation and sources remain
the [mounted-tine geometry references](physical-model-round5-kalimba-references.md).

`KalimbaMountedBridgeRatio` divides each mode's signed bridge displacement by
the fundamental's; the common spring stiffness cancels. One common per-key
output calibration preserves the previous fundamental gain. This is product
level calibration, not a newly measured force in newtons. Carving interpolates
the normalized mounted and rigid reactions with the same existing 0.65 blend
as its shape morph. The morph and the separately interpolated frequency/loss
tables remain approximations between FEM points; they are not a new boundary
solver. No controls, modal poles, per-sample processing, or generated tables change.

At C4 the first overtone's board-force ratio falls by 1.7 dB relative to the
fundamental. The sixth mode lies close to a bridge node: its ratio magnitude falls from
337.4 to 0.85; the fifth and sixth modes also change polarity. These are
mechanical transfer ratios, not the resulting acoustic partial levels; thumb excitation, board
response and capture still determine what is heard.

`Tests/KalimbaBridgeReactionTest.cpp` independently checks spring-energy
consistency against the existing generated loss/frequency tables at all
12 fitted points, where `reaction_ratio^2 = bridgeLoss * frequency_ratio`.
Maximum relative error is 1.99e-7. It also checks 5,224 actual voice modes
across notes 12–108, two internal rates and five Carving settings, preserves
the fundamental exactly, and checks signed interpolation through modal nodes.
It fails against the previous voice. The fundamental denominator stays above
0.0306 across the supported lookup range, including its held endpoints.

The existing Round6, Round7, Kalimba pickup and bounded-excitation contracts
also pass; natural-thumb F-sharp4 tink remains -27.66 dB relative to its
fundamental at 0.1 seconds. A fixed 16-note natural-thumb phrase with identical
input and gain changes Main RMS from -16.618 to -16.621 dBFS and Close RMS from
-29.170 to -29.175 dBFS. This establishes model consistency and bounded output;
listener realism has not been established by a blind comparison with recordings.
