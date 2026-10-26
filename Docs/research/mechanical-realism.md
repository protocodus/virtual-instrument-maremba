# Mechanical realism and mounted pickup

## Subsequent source update — 2026-09-26

The [Piezo and output update](piezo-and-output.md) adds an explicit sensor,
cable and amplifier-input equivalent circuit to the existing mounted pickup.
The Kalimba's shared plate now uses local curvature weights and passes through
the pickup mechanics and electronics, including their decaying state. Main L/R
also receives a separate clean 6.02 dB calibration before limiting; direct
stems retain their trim. These changes follow the packaged 1.4.0f1 candidate
and have not rebuilt the plugin or Recon installation.

The sections and recorded results below describe the earlier 2026-09-22 pass.
They remain historical evidence for that implementation, rather than results
for the subsequent source changes.

## Original update — 2026-09-22

This is an intentional sound update built on the current 1.3 development tree.
Existing parameter IDs, ranges, default settings and 2x quality are retained.
Saved patches load, but Artifacts, Piezo and instrument response intentionally
sound different. These are voiced physical approximations, not measurements of
particular instruments or pickups. No upstream synchronization is claimed.

## Artifacts is a playing control

The earlier implementation multiplied a noisy strike clack and added decaying
white-noise rattle. That scaling and independent rattle generator are removed.
A brief primary contact clack remains at every Artifacts setting; friction is a
small modulation only while the mallet is touching the bar.

At note-on a fixed number of deterministic random draws chooses an event plan:

- A **glancing stroke/incomplete pluck** moves the contact position, softens it
  and reduces its impulse. It changes the pitched excitation itself.
- A **rebound/thumb recatch** delivers a smaller positive contact pulse after a
  model-dependent delay, exciting the same ringing modes.
- A **neighboring-key brush** excites two pitched modes of an adjacent bar or
  tine after a short delay. The main note still sounds; a weak wrong note joins
  it. The voiced neighbor spacing is two semitones for bars and three for tines.
- **Mounting collisions** occur when the actual vibrating fundamental crosses
  a clearance gap. Closing velocity sets a bounded impact into two short rail,
  cord or bridge modes. Contact hysteresis avoids repeated triggering at the
  same crossing, and finite contact pulses avoid single-sample clicks.

Rebound and brush impulses are taken out of the initial stroke's impulse budget.
Collision amplitude is limited relative to the current bar ring (including
already stored mounting energy) and loses strength over
55–90 ms, while its resonances ring freely. This one-way collision layer does
not subtract energy back from the bar and is not an energy-conserving coupled
solver. It remains bounded and has no independent noise source.

The knob increases both event incidence and severity. Rare playing errors use
an amount-squared probability; loose mount incidence rises linearly. Thus a
particular hit can remain clean even at maximum. Repeated notes expose the
range. Zero disables event excitation completely. Fixed note-on random draws
and time-based integrated pulses keep reset renders deterministic and avoid
sample-rate-dependent event choices. Collision contact duration is also capped
relative to the note period so treble strikes do not skip alternating contacts. The knob takes effect on new strikes.

## Different responses at matching settings

| Instrument | Contact and controls | Imperfections and mounting |
| --- | --- | --- |
| Rosewood | Broad concert-mallet contact; long decay and tube loading | Restrained rebounds and adjacent-bar brushes; rail/adhesive response |
| Padauk | Firmer, shorter footprint; tighter decay range and broad soundbox loading | More short wooden knocks and rebounds; lower pickup sensitivity |
| Balafon | Wider, more compliant contact; stronger velocity compression and later coupling response | More grazing strokes, rebounds and loose-mount collisions |
| Kalimba | Finite thumb release becomes much quicker with Hardness; Decay extends the tine more than the cavity | Adjacent-tine brushes and bridge impacts; fewer recatches |

`InstrumentResponseProfile` holds these curves and event tendencies separately
from the existing modal frequencies and damping. Unity Decay remains unity;
Coupling at zero now has zero extra resonator loading. Existing signed beam
position nodes and instrument note ranges are retained.

## Piezo mounted on the instrument

The pickup no longer sums the same bar outputs with a generic treble weighting.
It samples signed bending curvature across a finite adhesive footprint. Each
instrument has a different mounting location and rail/bridge response; the
Kalimba pickup sits near the clamp. Since the tine bank represents velocity,
its displacement is recovered from the modal quadrature before measuring strain.
The wooden-bar bank already represents displacement.

Transmitted strike force and mounting impacts also excite the pickup. Two
broad mechanical resonances represent the body/rail and the bonded patch.
A capacitive-source/load high-pass rejects static strain, and a low-pass models
adhesive/sensor bandwidth. This path receives neither cavity air nor room reverb.
The acoustic frame is driven from a separate mechanical tap ahead of these
pickup electronics, so changing the sensor response cannot color acoustic Bloom.

All new state is per voice and allocation-free during rendering. Reset, voice
stealing, damping, energy accounting, subnormal flushing and retuning include
the extra states. Secondary pitched modes follow pitch bend; physical mounting
resonances stay fixed. The pickup can ring out briefly after its input stops.

## Validation and listening

Run `bash Tests/run_all_tests.sh`. The standard DSP, touch, wrapper and factory
patch suites are supplemented by:

- `MechanicalArtifactsTest.cpp`: zero-off, fixed draw counts, original impulse
  budget, valid timing, event incidence, touch dependence and model tendencies.
- `CollisionResponseTest.cpp`: isolated collision audibility, coherent ring
  bounds, treble-rate consistency, clean-restrike neighbor tails and stop.
- `PiezoPickupTest.cpp`: DC rejection, force-only pickup response, finite tails,
  reset and frequency-response consistency across internal sample rates.
- `EngineRealismTest.cpp`: gain-matched Close/Piezo distinction, deterministic
  repeated-hit artifact spectra and delayed energy, cross-rate event renders,
  and distinct hardness/position/coupling/decay responses.

The previous single-hit Artifacts audibility assertion is replaced by a fixed
64-strike ensemble: an occasional imperfection must not be required on every
arbitrary strike. The public-audio probe also fails against the saved prior DSP,
which guards against accepting a gain change as the requested fix.

`python3 Demos/build_realism_review.py --gain 2` creates fixed-common-gain WAV comparisons and a
local listening page. It freezes before/after source trees, renders identical
scores and reports hashes and levels; no clip is normalized individually.
Evidence for this run is in `Release/validation/engine-realism-20260922/`.
The isolated collision tests also reproduce four regressions when their fixes
are individually removed: coherent bridge takeover, skipped treble contacts,
clean-restrike neighbor retuning and a stale pitch-bend reference.

Native tests and timing do not establish an interactive Reason audition or
Reason scheduler/target-chip performance.

## Design references

[Bilbao et al., collision modeling](https://arxiv.org/abs/1405.2589) motivates
separating collision forces from the freely vibrating system and attending to
energy bounds; this implementation does not reproduce that paper's solver.
[The measured kalimba bridge study](https://pubmed.ncbi.nlm.nih.gov/22280717/)
illustrates the importance of bridge boundary conditions; the retained ideal
cantilever modes are still an approximation.
[Piezo Systems application notes](https://support.piezo.com/article/116-application-notes)
describe strain sensing and electrical loading. They do not supply our model's
mounting positions, filter frequencies or voicing gains.

## Recorded results — 2026-09-22

The complete local suite passed, including all 11 factory patches and the four
new realism contracts. The public-audio negative control against the saved old
engine failed 160 assertions. New artifact difference-signal energy varies by
at most 0.104 dB across the tested rates and C3/C5/C7 ensembles. The minimum
Close/Piezo gain-fit residual in the clean pickup matrix is 1.55%; this measures
waveform difference, not a perceptual listening score.

Apple clang 21.0.0 / arm64 / `-O3 -std=c++17`; 48 kHz, 64 frames, 16 voices,
4,096 measured blocks with periodic restrikes; deadline 1,333.33 µs:

| Quality | Median thread CPU | Wall / thread-CPU deadline misses |
| --- | ---: | ---: |
| Default 2x, before | 76.834 µs | 0 / 0 |
| Default 2x, after | 89.792 µs | 0 / 0 |
| 4x, after | 175.583 µs | 0 / 0 |
| 8x, after | 347.916 µs | 4 / 0 |

Default median CPU increased 16.9%. The final 8x stress run still had four wall
clock deadline misses. A prior run before the clean-restrike fix had thirteen
8x wall misses and two thread-CPU misses, and one 4x wall miss; its results are
retained rather than replaced by the final run. These are native measurements
and do not establish Reason scheduler or target-chip performance.
