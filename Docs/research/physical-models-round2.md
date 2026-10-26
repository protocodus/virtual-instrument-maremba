# Second physical-model pass

Historical snapshot: the [third pass](physical-models-round3.md) subsequently
replaces the ideal Kalimba cantilever and revises contact/support behavior.

This pass starts from the completed first physical-model update, saved under
`Release/validation/physical-models-round2-20260922/sources/before`. It keeps the
1.3.0b4 development identity, property contracts, defaults and 2x quality policy.
It intentionally changes acoustic tone. The first pass's evidence remains in
its original validation directory.

## Directional modal radiation

Close and Far now receive different complex projections of each upper bending
mode. The projection sums signed motion over the bar/tine surface, including
distance weighting and the relative propagation phase from each surface point.
Opposite-moving regions can cancel acoustically, while their mechanical motion
and contact-pickup signal remain present. This supplies mode-dependent color
and phase rather than another broadband microphone gain.

Rosewood, Padauk and Balafon use different approximate bar dimensions; Kalimba
uses a short, narrow tine and a closer pickup point. The signed shapes reuse the
existing free/free or cantilever approximation. Fixed spatial quadrature tables
avoid recomputing those shapes during strikes. Geometry/propagation coefficients
are calculated at note-on, with no per-sample trigonometry or allocation.

The fundamental retains its previous calibration. Upper projections are blended
with the existing voiced readout and cannot boost a mode above its former
amplitude. The correction is stronger at Far than Close. Mechanical feedback,
body drive, pickup electronics and cavity energy do not depend on these acoustic
readout coefficients.

This is a bounded top-surface radiation approximation. It omits underside
motion, edge diffraction and frame scattering; dimensions and microphone
positions are voiced values. Removing common propagation time avoids introducing
device latency, so this is not a simulation of an arriving wavefront. The
projection is latched per strike: pitch bend moves the oscillator, while the
spatial transfer is recalculated on the next strike. The approximation and
primary evidence are detailed in [the research notes](physical-model-round2-references.md).

## Continuous multimode cavity exchange

The previous upper-air model selected one nearest bending mode. Crossing a
selection boundary could change the driving mode abruptly, especially with the
fixed Kalimba soundbox. Each upper air mode now sees a smooth combination of its
two bracketing bending modes. Log-frequency weights account for modal losses,
then normalize to one coupling direction.

Pressure exchanges energy with the velocity projected onto that direction;
the returned velocity change is distributed to both bending modes. This single
orthogonal exchange preserves their summed quadratic energy, including during
restrikes. The fundamental aperture remains anchored to the fundamental bending
mode. Pitch bends update the overlap without moving the Kalimba box frequencies.
The overlap law is a reduced approximation, not a measured aperture matrix.

## Cost and control behavior

The cavity junction caches its coordinate transforms, tine strain caches its
frequency scaling, and the membrane caches constant integration factors.
Keyboard pan gains are computed once per engine instead of four trigonometric
calls per voice/sample. An optimized-only snapshot separates these arithmetic
changes from the intentional sound changes.

Validation includes existing touch/velocity/position/decay and parameter
contracts, new cavity-boundary convergence tests, radiation ablation, spatial
quadrature checks, shared-resonance rate comparisons and membrane release.
Performance probes cover all four models at 2x/4x/8x and a separate default-2x
workload that restrikes 16 notes together every 32 blocks. Native timing is
reported separately from Reason/Recon host behavior.

## Measured results

The SDK 5 local45 Deployment build passed for Intel and Apple Silicon, including
the mandatory suite and all 11 factory patches at 44.1/48/96 kHz. The final
radiation-ablation extension also passed separately at O3. Cavity, second-round
response and radiation-geometry contracts passed AddressSanitizer and
UndefinedBehaviorSanitizer. No additional factory patch adjustments were needed.

On this Apple M1 Max, 48 kHz, 64-frame, 16-voice native probe:

| Model | First-pass median CPU/block | Second-pass median CPU/block | Reduction |
| --- | ---: | ---: | ---: |
| Rosewood | 200.7 µs | 181.0 µs | 9.8% |
| Padauk | 176.0 µs | 163.3 µs | 7.2% |
| Balafon | 196.5 µs | 177.0 µs | 9.9% |
| Kalimba | 164.7 µs | 139.0 µs | 15.6% |

All four default-2x cases and all four 16-note restrike cases had zero wall or
thread-CPU deadline misses. Computing radiation at strikes raises the dense
restrike p99 CPU time to 317–404 µs from 241–299 µs; it remains below the
1333 µs block deadline in these runs. Optional 8x still had nine wall-clock and
six thread-CPU overruns across 16,384 blocks. All raw runs are retained, without
retrying for a clean timing result. These figures are not a Reason host guarantee.

The optimized-only voice comparison covers 240 cases and 36.3 million samples:
maximum absolute error is 2.98e-8 and relative RMS error is −195.3 dB. Keyboard
pan caching is bit-identical over 13,400 comparisons. New two-mode junction
energy error is below 2.1e-11 in the isolated repeated-exchange test. Shrinking a
cavity tuning interval tenfold now reduces its audio discrepancy by about 20 dB
across all ten tested former mode-switch boundaries.

The [24-clip before/after review](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Release/validation/physical-models-round2-20260922/physics-review.html)
compares this pass directly with the first pass at common gain. It includes dry
Close, the default mix and hard off-centre contact through dry Far. The default
score changes little; the hard Far comparison exposes the wood-bar changes
more clearly. Kalimba's radiation contribution remains smaller, consistent
with its compact surface and restrained blend. No completed subjective listening
assessment or interactive Reason/Recon playback is claimed.

See the [validation manifest](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/physical-models-round2.json) for exact
sources, native payload, logs, audio measurements and complete timing rows, and
the [parameter audit](../validation/round2-parameter-audit.md) for the ablation and
continuity results.
