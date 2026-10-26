# Kalimba: consistent mount geometry and further physical-model leads

Research and implementation reviewed on 2026-09-24. This is an intentional
physical-model change, not a bit-identical CPU optimization or a reconstruction
of a measured commercial instrument.

## What the previous model omitted

Round 4 derived note-dependent frequencies and bridge losses from a beam held
by a pinned pressure bar behind a yielding bridge. It nevertheless excited
those modes using a different, rigid-bridge beam with a fixed rear/free length
ratio. The piezo still read a third geometry, an ideal cantilever. Thus a
partial could be assigned a frequency, an excitation node, and a pickup strain
that did not belong to the same vibration mode. These differences matter most
for upper partials and short treble tines.

The new `KalimbaMountShapes.h` retains the round-4 frequencies and losses and
supplies the corresponding mode shapes for finite thumb footprints, contact
loss, spatial microphone projection, and piezo curvature. This completes the
existing yielding-mount model; it does not add a shared soundboard or a new
nonlinear thumb solver.

## Research basis and limits

Chapman's measurements and beam analysis establish that the bridge subdivision
changes kalimba overtone ratios and mode shapes. This supports deriving
excitation and readout from the actual mount rather than treating every tine
as an ideal cantilever. The yielding-spring interpretation and its stiffness
fit are engineering choices inherited from round 4; Chapman did not supply
the current spring, pickup gains or soundbox calibration.
[Chapman 2012, primary abstract](https://pubmed.ncbi.nlm.nih.gov/22280717/).

The earlier five-tine measurements give a useful mounting/length calibration
target, and report that touching the rear span damps the sound. They do not
identify a complete frequency-dependent bridge impedance.
[Chapman 2006, primary paper](https://jcaa.caa-aca.ca/index.php/jcaa/article/download/1805/1552/1942).

A more recent kalimba design study combines analytical eigenfrequencies, FEM
with realistic geometry/boundaries and physical validation. Its relevance here
is the validation workflow, not a source of new measured constants for this
device. Only the publisher's accessible description was used.
[Vadillo et al. 2025](https://journals.sagepub.com/doi/10.1177/03064190251371151).

## Derivation and numerical representation

The offline generator assembles the same consistent-mass cubic Hermite beam
as `generate_kalimba_mount.py`. With free playing length 1, rear length `b/a`,
and normalized bridge stiffness `kappa = k*a^3/EI`, it solves

```
(K_beam + kappa*e_bridge*e_bridge^T) phi_n = omega_n^2 M phi_n
phi_i^T M phi_j = delta_ij
```

The pressure bar fixes displacement, allowing slope; the playing tip has zero
moment and shear. Displacement, slope and moment are continuous at the bridge,
and the shear jump balances the spring force. Full-beam modal mass includes
the rear span, even though only the exposed span receives the thumb contact.

For each of the existing 12 frequency-grid points and 8 modes, the playing
eigenvector is compressed into the beam's four bounded analytical functions:

```
phi(x) = ep*exp(beta*(x-1)) + em*exp(-beta*x)
       + c*cos(beta*x) + s*sin(beta*x),  0 <= x <= 1
beta = sqrt(omega)
```

Every exponential argument is nonpositive. The coefficients are fitted to the
FEM eigenvector, not fitted to an audio sample. Analytic finite-width integrals
give the signed excitation `average(phi)`. Contact damping uses a two-point
Gaussian approximation of `average(phi^2)` so a thumb spanning a node still
touches moving material. The pickup reads `average(phi'')` at the established
position 0.06 and width 0.075. A common positive per-key pickup scale preserves
the old fundamental level while retaining relative signed modal curvature.

Mode signs have the existing alternating free-tip polarity. The fundamental
contact weight is normalized to one at the default point 0.825; the upper
weights retain 0.5 whole-beam RMS normalization. These output/contact scales
are disclosed voicing normalizations, not additional mass or measured gain.

Responses interpolate linearly in log-frequency between neighboring mount
solutions. Interpolated functions are a reduced approximation, not an exact
intermediate-boundary eigenproblem. Below C3 and above A5 the table endpoints
are held, matching the frequency/loss table. Frequency bends do not imply a
new physical tine length during a ringing note.

Radiation uses a frozen `[12][8][32]` float table at the existing Gauss-Legendre
nodes: 12 KiB shared immutable storage, no per-voice state. Runtime radiation
needs only interpolation of two values per node. Analytical contact/strain
work runs at trigger time; no additional oscillator, allocation, solver, or
per-sample branch is needed.

## Validation of the geometry helper

On the development machine:

| Check | Largest error/result |
| --- | ---: |
| Analytical playing-shape fit against 240-element FEM | 1.408e-7 absolute |
| Scaled physical boundary residual, both spans | 4.455e-7 |
| 240 vs 480 free-span elements: normalized shape | 8.972e-8 absolute |
| 240 vs 480: mean pickup curvature | 3.979e-6 relative, unit absolute floor |
| C++ frozen shape frequencies vs existing frequency table | 4.743e-8 relative |
| C++ finite footprints vs independent Simpson integration | 7.365e-12 absolute |
| C++ radiation cache vs independently computed Gauss roots | 5.961e-8 absolute |
| C++ pickup curvature vs fourth-order numerical derivative | 6.513e-12 relative, unit absolute floor |
| Contact jump across table keys at ±1e-8 relative frequency | 3.530e-9 absolute |
| Largest contact weight in the bounded sweep | 1.263 |
| Largest pickup curvature weight in the bounded sweep | 83.226 |

Large upper curvature weights are expected from a second spatial derivative;
they are not acoustic modal amplitudes. The velocity-bank readout separately
converts to displacement and the excitation includes the modal release factor.
These helper tests prove geometric consistency and bounded coefficients;
integrated voice audio, rate, stress and CPU checks remain necessary.

```
OPENBLAS_NUM_THREADS=1 python3 Tests/generate_kalimba_shapes.py
clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP \
  Tests/KalimbaMountShapesTest.cpp -o /tmp/maremba-kalimba-mount-shapes
/tmp/maremba-kalimba-mount-shapes
```

## Additional improvements ranked for later work

| Lead | Expected sonic benefit | CPU and stability considerations |
| --- | --- | --- |
| A shared, reciprocal tine–bridge–soundboard network | High: note-dependent body coloration, energy transfer and common-board memory across chords; current box state is per voice | Several shared board modes could be inexpensive, but their mobility must be measured/fitted and the coupling passive; avoid double-counting the existing bridge loss |
| Finite thumb/nail contact with the ringing tine | Medium–high: the old ring reacts to the player's local preload and release instead of independent modal attenuation | A rank-one contact solve only during a short release; requires correct mass coordinates and tests for passivity, separation and rate convergence |
| Offset-mount one-sided contact and slip | Potentially high for hard attacks: transient harmonic components in addition to the inharmonic bending modes | Nonlinear and alias-sensitive; needs energy-stable contact and a measured preload/clearance range before production voicing |
| Actual tine taper, curved tip, thickness and bridge impedance measurements | High for matching a particular instrument rather than a family | Offline FEM/table fitting can leave runtime cost unchanged; reference recordings alone cannot uniquely identify the mechanical parameters |

The offset-mount lead has direct experimental motivation: Ludwigsen and Huizar
observed integer-harmonic peaks as well as beam-mode peaks after releasing an
initially deflected beam under an offset clamp. The source does not establish
the exact contact law or gains appropriate to our virtual instrument.
[Ludwigsen and Huizar 2020, primary institutional abstract](https://digitalcommons.kettering.edu/naturalsci_facultypubs/5/).

For future nonlinear contact, recent explicit energy-quadratisation schemes
offer a useful alternative to unbounded iterative solves. A 2024 comparison
addresses spurious contact oscillation and non-adhesion, and compares modal
with finite-difference implementations. This supports testing more than one
scheme and timestep rather than assuming passivity alone ensures good audio.
[Van Walstijn, Chatziioannou and Bhanuprakash 2024](https://doi.org/10.1016/j.jsv.2023.117968).

A related 2024 extension includes contact damping and dry friction with a
passivity proof for a two-dimensional impact oscillator. It is an appropriate
research starting point for a thumb slipping off a tine, but not a validated
kalimba implementation. No code or paper content is copied into this device.
[Van Walstijn, Chatziioannou and Athanasopoulos 2024, author-hosted paper](https://pureadmin.qub.ac.uk/ws/portalfiles/portal/613541822/1-s2.0-S2405896324010255-main.pdf).
