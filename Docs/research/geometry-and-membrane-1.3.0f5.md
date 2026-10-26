# Consistent instrument geometry and nonlinear membrane material

> In 1.4.0b1 this pass is [combined](combined-models-1.4.0b1.md) with the fifth
> and sixth physical-model passes: its mounted kalimba shapes and membrane
> material carry over; its bar geometry and bar pickup are superseded by the
> fifth pass's carved bars. The text below describes 1.3.0f5.

This source update, developed on 2026-09-24, makes the four instrument models
more physically consistent. It intentionally changes their sound and follows
the packaged 1.3.0f4 candidate. No new control, preset, parameter identity or
default is introduced. The smooth Strike Variance response is retained. Both
changes are included in the [1.3.0f5 candidate](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.3.0f5.md).

## Implemented improvements

| Instrument | Changes | What this corrects |
| --- | --- | --- |
| Imperial Rosewood | An offline undercut-beam model provides all eight bending frequencies, signed strike shapes, finite contact footprints, pickup curvature and upper-mode microphone radiation. The cut becomes shallower through C5–C7. | A tuned 1:4:10 bar previously used the strike nodes and pickup of a different, uniform beam. Upper frequencies and spatial response now change together with the cutaway. |
| Mayan Padauk | A separate undercut family fitted to 1:4:9.8, with the same consistent excitation/readout paths; positive cubic stretching and strain-dependent loss in the charleo membrane. | Preserves its shorter, firmer voicing while giving contact position and buzz dynamics a coherent structural basis. |
| Balafon Ancestral | Its own 1:4:9.6 undercut family, plus stronger membrane stretching and strain loss than Padauk. | The mirliton becomes stiffer as a taut excursion grows, so stronger strikes change its spectrum and release, beyond simply increasing buzz level. |
| Kalimba Artisan | Contact, footprint loss, radiation and pickup now use the same yielding bridge and pinned pressure-bar modes as tuning and bridge loss. | Replaces the three inconsistent geometries previously used for pitch, plucking and pickup. No extra per-sample oscillator is needed. |

The generated wooden shapes retain their signed nodes and mass normalization.
A single force-calibration scalar per key preserves the established centre
fundamental reference as its cut changes; it never normalizes the actual strike
position or individual modes. Pickup voltage retains its original fundamental
reference. Padauk's pickup output gain is reduced from 0.78 to 0.62 to reserve
headroom for stronger upper-mode strain under hard contact. These are disclosed
instrument voicings, not measured mallet force or transducer sensitivity.

The existing finite-mass restrike correction receives the new contact shapes.
Accidental neighboring-bar brushes also use the neighbor's solved treble ratio.
The separate brush, torsion, body and cavity subsystems remain reduced models;
this pass does not turn them into one fully coupled three-dimensional solve.

## Research and approximation boundaries

The bar work follows the connection between geometry and modal tuning in
[Beaton and Scavone, JASA 2021](https://caml.music.mcgill.ca/lib/exe/fetch.php?media=publications:beaton_idiophonebartuning_jasa_2021.pdf)
and the FEM/BEM optimization direction in
[Panagiotopoulos and Kouzoupis, Applied Acoustics 2024](https://doi.org/10.1016/j.apacoust.2024.109850).
The implementation is a one-dimensional Euler–Bernoulli reduction with voiced
family geometries; it is not the papers' full 3D model or a scanned instrument.
The 2024 publisher abstract/introduction, rather than an inaccessible full
paper, was used. Details, derivation and validation are in
[the bar research notes](physical-model-round5-bar-references.md).

The Kalimba completes the existing model informed by
[Chapman's five-tine measurements](https://jcaa.caa-aca.ca/index.php/jcaa/article/download/1805/1552/1942).
Its spring interpretation and electrical calibration are engineering choices.
[The Kalimba notes](physical-model-round5-kalimba-references.md) distinguish the
measured targets, FEM boundary problem and runtime interpolation.

The membrane's quartic potential and positive strain loss are informed by
[published nonlinear membrane elasticity](https://arxiv.org/html/1407.4904).
The material coefficients are voiced, not inferred from spider silk or gut.
A convex discrete-gradient update accounts for pressure work and positive loss.
[The membrane notes](physical-model-round5-membrane-references.md) separate this
implemented law from multiple membranes and fully reciprocal acoustic loading.

## Further improvements, ranked by practical value

| Instrument | Next work | Evidence and cost |
| --- | --- | --- |
| Rosewood | Solve mallet compression, separation and rebound against the moving bar; derive tube-mouth coupling from surface geometry. | [DAFx2024 quadratic contact potentials](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_40.pdf) offer an energy-stable collision method without an iterative root solve. This requires integrating a common mallet/modal energy convention, not simply replacing a force function. Geometry-derived acoustic coupling can be reduced offline. |
| Padauk | Fit measured per-mode loss and mobility; give the charleo a reciprocal reactive load on its box. | Species names do not determine decay constants. Bare-bar, assembled and pickup recordings are needed. Reactive membrane loading needs jointly normalized membrane/air states and additional stability validation. |
| Balafon | Use two or more differently tensioned membranes and fit actual gourd admittance and bar/gourd alignment. | The [primary gyil modeling study](https://webhome.csc.uvic.ca/~gtzan/smc2012model.pdf) describes multiple membranes and broadband buzz. Extra nonlinear updates have a real CPU cost; recorded gourds are needed for calibration. |
| Kalimba | Add one shared reciprocal soundboard/bridge, finite thumb recatch and release, and specimen-specific rear-span geometry. | Current per-voice cavities cannot represent every cross-tine interaction. A reduced board impedance can remain efficient; coupled nonlinear thumb contact requires new energy and restrike tests. |
| All four | Fit parameters against controlled recordings, then validate on held-out notes, positions and velocities. | [Differentiable modal simulation research, 2025](https://arxiv.org/html/2505.05940v1) motivates offline inverse fitting. No JAX, neural network or GPU dependency is added to the realtime instrument. |

More states alone do not establish greater fidelity. This pass removes concrete
geometric contradictions and adds a validated material mechanism. Claiming an
exact acoustic counterpart still requires reference measurements and listening
comparisons; neither a converged FEM nor passing audio tests supplies those.

## Reproduction and evidence

Run `Tests/run_all_tests.sh` with `JUKEBOX_SDK_DIR` pointing to SDK 5. The runner
includes new bar, mounted-tine, membrane and whole-voice contracts. Generated
fixtures can be independently checked with `Tests/generate_bar_modes.py` and
`Tests/generate_kalimba_shapes.py` (NumPy/SciPy, offline only).

The development evidence directory is
`Release/validation/physical-models-round5-20260924/`. It retains the frozen
pre-change DSP, source hashes, initial failing runs, final contract logs, native
timings and the `comparison-reviewed/physics-review.html` listening player. The baseline
is e9de63d plus the accepted smooth Strike Variance change, so comparisons do
not confound the geometry/material work with the earlier variance adjustment.

The player provides four setups for each instrument, with the same score and
random sequence before/after: dry Close, engine-default mix, hard off-centre
dry Far, and quiet restrikes of a ringing note. Every WAV uses one common gain;
optional RMS matching attenuates playback only. These are focused A/B renders,
not a claim that a human listening panel preferred the new version.


## Validation results

- The complete local suite passes, including the wrapper host contract and all
  eleven factory presets across its sample-rate matrix. Default full-velocity
  chords retain at least 4.37 dB of direct-output headroom.
- The engine suite and new whole-voice contract pass AddressSanitizer and
  UndefinedBehaviorSanitizer. The standalone geometry and material contracts
  also pass sanitizers. Generated bar and Kalimba fixtures verify independently.
- SDK `JukeboxSDK_500_028` builds both arm64 and x86-64 local45 Deployment modules
  with its normal static analysis. Output was redirected to the task-owned
  evidence directory. The user's installed device was not replaced.
- The actual SDK-built arm64 module renders all eleven factory presets through
  the previously audited local45 ABI fixture, with all seven outputs connected,
  finite-audio checks and allocation traps active. The largest peak is 0.7251
  (−2.79 dBFS). This is a simulated host render, not a live Reason session.
- The membrane kernel checks 864,000 forced/free updates, with maximum relative
  work-balance error 6.69e-13 and no passive energy growth. At full Buzz, isolated
  material changes produce phase/gain-independent spectral differences up to
  0.173% for Padauk and 1.867% for Balafon; Buzz zero remains bit-identical.
- Bar mesh refinement changes ratios by at most 0.00443 cents. Interpolation
  between 17 cut depths has a measured worst midpoint error of 4.84 cents;
  interpolation is not an exact intermediate FEM solve. The largest integrated
  radiation-readout error against dense quadrature is 0.00766.

The initial integration failures are retained. They identified missing common
force calibration in the contact reaction, pickup headroom and a repeat-hit
frequency probe that ignored fixed per-key scatter. Calibration and the probe
were corrected; existing sensitivity, brightness and output bounds were not
relaxed. Tests that intentionally locate a fundamental strike node now locate
it on the undercut geometry rather than assuming the uniform-bar node.

## CPU cost

Native Apple M1 Max measurements use 48 kHz, 64-frame blocks, 16 voices, 256
warm-up blocks and 4096 measured blocks per case. Two baseline/current pairs
run in opposite orders for the 2x/4x/8x matrix; separate single pairs cover
16-note restrike bursts and maximum Buzz/velocity. No other task builds ran
while timing. The baseline includes the accepted smooth Variance curve.

| Instrument | Default 2x: median block CPU before → after | Change | Maximum Buzz 2x change |
| --- | ---: | ---: | ---: |
| Rosewood | 167.02 → 170.10 µs | +1.85% | +0.91% |
| Padauk | 166.25 → 172.25 µs | +3.61% | +1.21% |
| Balafon | 166.90 → 170.56 µs | +2.20% | +1.68% |
| Kalimba | 139.04 → 139.02 µs | −0.02% | +0.83% |

The primary statistic is the median of per-run thread-CPU medians. This pass is
an intentional model upgrade, not a further CPU reduction. The burst workloads
measure 0.34–4.75% lower medians, but one stress pair is not a general speedup
claim. Added geometry work occurs at note-on and uses shared immutable data;
material stretching adds arithmetic and a bounded nonlinear solve when taut.

All default-2x cases, including the stress pairs, have zero thread-CPU deadline
misses against 1333.33 µs. Wall-time misses remain and are recorded separately.
The 8x matrix still misses deadlines: 17 baseline versus 15 current blocks
charged to the measured thread; these stress failures are not discarded or
explained away. At 4x the baseline has one charged miss and current has zero.
The full run retains all 262,144 measured blocks' per-case summaries; the probe
records distributions and miss counts, not individual block traces.

These are native engine measurements. They do not establish Reason scheduler
performance, universal target translation, a completed listening preference
test, or release acceptance. Separate f5 build and host evidence is recorded in
[the release report](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.3.0f5.md). The existing 1.3.0f4 release
artifacts are unchanged.

The compact, source-pinned record is
[`validation/physical-models-round5.json`](../validation/physical-models-round5.json).
