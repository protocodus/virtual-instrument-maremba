# Combined models — 1.4.0b1

Two lines of work grew in parallel from 1.3.0f1:

- **1.3.0f2 – 1.3.0f5**: a feedback update (Buzz range, 16 voices, Room and
  Drive levels, rear icons painted into the panel), CPU work on the
  sympathetic halo, a smoother Strike Variance, and a geometry pass
  ([Consistent instrument geometry and nonlinear membrane material](geometry-and-membrane-1.3.0f5.md)).
- **1.4.0b1**: the [fifth](physical-models-round5.md) and
  [sixth](physical-models-round6.md) physical-model passes.

1.4.0b1 joins them, as an experiment: where both lines modelled the same
mechanism, the combined engine keeps one model and says why; where only one
line had it, it is carried over; product decisions from the feedback update
apply to the whole bank. No real instrument was recorded for either line and
nothing here is a listening test.

## Mechanism by mechanism

| Mechanism | 1.3.0f5 | 1.4.0b1 | Combined |
| --- | --- | --- | --- |
| Bars: tuning, strike shapes, radiation | One smooth undercut per family (Euler–Bernoulli FEM, eight modes fitted to 1:4:10, 1:4:9.8, 1:4:9.6, shallower toward the treble), shared by tuning, contact and radiation | Every key carved in a Timoshenko FE model (sixteen modes, torsion and lateral bending, Hill's decay, Carving) | 1.4.0b1's carved bars. They keep the 1.3.0f5 principle (one geometry for pitch, strike and radiation) key by key; its bar table, generator and bar contracts are superseded |
| Bar pickup (Piezo) | Curvature of the undercut geometry; Padauk's pickup 2 dB lower for its stronger upper-mode strain | Uniform-beam strain weights on the carved bars' modal masses | 1.4.0b1's readout and gain. The curvature pickup is **not carried over** (open), and so neither is the trim made for it |
| Kalimba tine shapes | The pinned-bar / yielding-bridge FEM that sets the tines' pitches also gives their shapes, per key | Rigid-bridge analytic shapes, the same for every key | **1.3.0f5's mounted shapes**, in their mass-normalized form, for the thumb's contact and the radiation (`KalimbaMountedShape`) |
| Membranes | Positive stretching and strain loss once taut (Balafon 0.30 / 0.15, Padauk 0.12 / 0.08) | Two or three mirlitons per gourd, moving limits per slap, Tension, slack radiation | **Both**: 1.4.0b1's membranes with 1.3.0f5's stretching material on each; a slap's moved limits carry the material terms |
| Buzz range | The knob reaches 70 % of the former physical range (feedback) | Full range, recalibrated membranes and buzzers | **70 %**, on every entry path (strike, restrike, automation). The Guatemalan patches store their Buzz in the new knob units (below) |
| Strike Variance | Concave curve `a (2 − a)` on contact, impulse, shock and per-partial gains | Physical strokes: head stiffness and loss, stick speed, landing point, lateral offset | **1.4.0b1's physical stroke on 1.3.0f5's curve**, with wider maximum spreads so the control is as audible (below) |
| Sympathetic halo | Coupling table stored as contiguous runs (CPU) | Receivers on each key's own pitch and loss, driven through the cords (bars) or the bridge (tines) | 1.4.0b1's halo; the coupling table the optimization sped up no longer exists |
| Restrike contact | A finite-mass correction projected through the generated shapes | A Hertzian head solved each sample against the bar | 1.4.0b1's head (it already meets the bar through the bar's own shapes) |
| Rear panel | Output icons painted into the backdrop | Outputs packed to the left, Tune/Detune/Scale beside them | **Both** |
| Factory bank | 16 voices, Room ÷ 3, Drive ≤ 15 % (feedback) | Nine new patches, 1.4.0b1 voicing | **The feedback rules on all twenty patches** |

## Bank and controls

- All twenty patches play 16 voices, keep a third of their former Room level
  and at most 15 % Drive. The nine 1.4.0b1 patches follow the rule too
  (Kalimba Muted Thumbs and Bowed Rosewood Choir, since renamed Rosewood Roll Chorale when the Bow was removed, went from 24 to 16 voices).
- Buzz: stored patch values keep their meaning of a knob position, as in
  1.3.0f2, so most patches buzz 30 % less than before. The Guatemalan patches
  are the exception: their charleo must be heard at mezzo-forte (the fifth
  pass's finding for Guatemalan playing), so they store the physical amount
  they were voiced with in the new units — Chiapas Folk Fiesta 0.35 → 0.50,
  Mayan Dance Padauk 0.40 → 0.57, Guatemalan Charleo 0.75 → 1.00 (0.70 of the
  former range, the most the knob now reaches).
- Strike Variance: the spreads at 100 % are head stiffness ±38 %, stick speed
  ±8.4 %, landing point ±7.1 % of the length, lateral offset ±0.55 of the half
  width and head loss ±17 %; at 0 % they are the unchanged natural floor
  (±3 %, ±0.4 %, ±0.12 %, ±0.25, ±2 %). The curve reaches 9.75 % of the added
  spread at 5 % and 27.75 % at 15 %.

## Contracts

Both lines' contracts run in `Tests/run_all_tests.sh` and in CI. Changed on
joining, with reasons:

| Contract | Change | Why |
| --- | --- | --- |
| `BarModesTest`, `generate_bar_modes.py`, `DSP/BarModes.h` | 1.4.0b1's | The 1.3.0f5 bar table is superseded by the carved bars |
| 1.3.0f5 `Round5ResponseTest` | Now `MaterialGeometryTest`: the voice's radiation against a dense readout of its own shapes (carved bars and mounted tines), odd-mode parity, the membrane material A/B | Its bar-table API and pickup-curvature checks have no counterpart; 1.4.0b1 keeps its own `Round5ResponseTest` |
| Radiation against the dense readout | Within 0.01 below 8 kHz, 0.04 above | 32 Gauss points resolve the close microphone's near field through 8 kHz; the bass bars' modes 10–15 (gone within tens of milliseconds) read up to 4 % off |
| Odd-mode drive at the centre | Below 5 % of an off-centre stroke's (was exactly 0) | A player at Variance 0 still lands within half a millimetre |
| `StrikeVarianceTest` | Frozen endpoints and bounds of the physical stroke; Rosewood's audibility gates × 0.75 | The frozen generator is 1.4.0b1's; Rosewood's tube carries its ring's fundamental, a scalar the metric removes |
| `BuzzRangeTest` | Pins the physical amount the voice stores (its membranes derive gaps, drive and radiation from it); the kalimba probe board swings ten times harder | 1.4.0b1 has several membranes per key; its buzzers lift only on a pluck's jolt |
| `ContiguousCouplingTest` | Removed | The coupling table it checked no longer exists |
| Kalimba forte rattle (`Round4ResponseTest`) | Above −45 dB at full Buzz (was −40) | Full Buzz is 70 % of the former range: −40.8 dB now |
| Charleo at mezzo-forte (`Round5ResponseTest`) | At Buzz 0.50 and 1.00 (was 0.35 and 0.75) | The Guatemalan patches' new values |

## Open

- The bar pickup still reads uniform-beam strain; the carved bars' own
  curvature (1.3.0f5's idea) needs the generator to export it.
- The 1.3.0f5 voice-rendering and cavity-cache studies (`Tests/Fixtures/VoiceRendering`)
  were made against the 1.3 voice and do not apply to this one.
