# Undercut-bar reduction and further realism work

Research and implementation reviewed 2026-09-24. The wooden-bar change is in
`DSP/BarModes.h`; `Tests/generate_bar_modes.py` generates its data without adding
an eigensolver, Python dependency or allocation to the audio engine.

## The inconsistency addressed

The earlier Rosewood, Padauk and Balafon frequencies were tuned close to
1:4:10, while their strike, contact-loss, microphone and pickup functions used
uniform free/free bar shapes. That spatial beam instead has ratios
1:2.75654:5.40392. Consequently the same nominal bar had different mechanical
geometry for its frequencies and its playing positions.

A representative undercut beam now supplies eight eigenfrequencies and eight
signed spatial modes together. Its family targets are the existing first three
ratios: Rosewood 1:4:10, Padauk 1:4:9.8 and Balafon 1:4:9.6. Only those targets
were fitted. The other five ratios are predictions of this reduced beam, not
independently fitted or measurements of named instruments.

## Research basis and what it supports

1. Beaton and Scavone (2021), **Three-dimensional tuning of idiophone bar modes
   via finite element analysis**, *JASA* 149, 3758–3768.
   [Full author paper](https://caml.music.mcgill.ca/lib/exe/fetch.php?media=publications:beaton_idiophonebartuning_jasa_2021.pdf).
   The full paper was read, including the method, geometry and material tables.
   It connects carved geometry to modal frequencies and shapes; a usual marimba
   tuning is 1:4:10, and fewer modes are tuned in higher notes. Its rosewood F3
   example is 406 × 58 × 24 mm with a 6 mm minimum allowed thickness. The paper
   also demonstrates why torsion, lateral bending and longitudinal modes require
   a three-dimensional model. Our one-dimensional model captures vertical
   bending only; the existing separate torsion model is unchanged.

2. Panagiotopoulos and Kouzoupis (2024), **Tuning of idiophones using shape
   optimization on finite and boundary element 1D models**, *Applied Acoustics*
   217, 109850. [Publisher](https://doi.org/10.1016/j.apacoust.2024.109850).
   The publisher abstract and indexed introduction/conclusion were accessible;
   the full paper was not. It describes FEM and BEM shape optimization of bars
   and resonators. This corroborates the reduced geometric approach, but none of
   our numeric geometry constants is attributed to this paper.

3. Bhanuprakash, van Walstijn and Chatziioannou (2024), **Quadratic Spline
   Approximation of the Contact Potential for Real-Time Simulation of Lumped
   Collisions in Musical Instruments**, DAFx24.
   [Full proceedings paper](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_40.pdf).
   The full method and modal coupling equations were read. A convex quadratic
   spline approximates a one-sided contact potential, allowing a noniterative
   energy-stable collision update. This is a promising next replacement for the
   current prescribed mallet pulse, not a change implemented by the bar table.
   The paper also warns that some auxiliary-variable energy-quadratisation
   schemes can produce wideband artifacts despite energy stability.

4. Soares, Debut and Antunes (2023), **The bar-resonator interaction in mallet
   percussion instruments: A multi-modal model and experimental validation**,
   *Journal of Sound and Vibration* 548, 117528.
   [University publication record](https://researchportal.ulisboa.pt/en/publications/the-bar-resonator-interaction-in-mallet-percussion-instruments-a-/).
   The university abstract was read. It supports two-way coupling between sets
   of mechanical and acoustic modes, with coupling depending on separation,
   placement and mode shapes. It motivates replacing the remaining voiced
   cavity-overlap coefficients with geometry-derived ones; it does not establish
   our current coefficients as measured or experimentally validated.

## Reduced model and runtime functions

In normalized length coordinates, each family's positive thickness is

```
h(x) = 1 - d [max(1 - |(2x-1)/a|^p, 0)]².
```

The flat ends retain their full thickness. Thickness and its first derivative
are continuous at the cut boundary. A consistent-mass, cubic Hermite
Euler–Bernoulli beam is assembled by four-point Gaussian integration:

```
K = integral h³ B''ᵀ B'' dx,
M = integral h Bᵀ B dx,
K phi_m = omega_m² M phi_m.
```

Its two rigid modes are removed. Density, Young's modulus and overall dimensions
scale all frequencies together in this model, so runtime tuning sets the
fundamental exactly; they are not needed to fit the normalized ratios. The
remaining modes are mass-normalized. Their signs and overall normalization
match the old spatial convention in the uniform limit. Mass normalization
allows thinner portions of the bar to have different mobility; there is no
per-position gain normalization that would remove nodes.

| Family | Cut depth d | Extent a | Curvature p | Lowest thickness / end thickness |
| --- | ---: | ---: | ---: | ---: |
| Rosewood | 0.725719 | 0.840059 | 3.3 | 0.274281 |
| Padauk | 0.621593 | 0.686033 | 3.0 | 0.378407 |
| Balafon | 0.587941 | 0.648778 | 2.8 | 0.412059 |

These geometries are voiced representatives selected to satisfy the family
ratios; they were not scanned from instruments. Depth and extent are fitted,
while the curvature exponents select three feasible smooth-arch families.
The minimum-thickness proportions are plausible relative to the published
rosewood example, but that observation is not specimen calibration.

The table stores 17 cut depths from the full family cut to a uniform bar. The
existing C5–C7 smoothstep now selects these shallower geometries. Linear
interpolation of neighboring modal tables is continuous and bounded, but is an
approximation to an intermediate eigenproblem. The generator quantifies that
approximation against fresh midpoint FEM solves. All eight frequency ratios
and all spatial readouts select the same depth. Each mode retains its parity
and node count across this transition; no sorting by instantaneous waveform
amplitude is used.

A 65-point half-bar table stores displacement, slope and cumulative integral.
Cubic Hermite interpolation retains exact mirror parity. `BarContactWeight`
uses the analytic signed footprint integral. `BarContactParticipation` integrates
the square of that same cubic, so an odd mode can have zero net excitation yet
still lose energy to a mallet spanning its node. `BarModeStrainWeight` uses the
same mode's endpoint-slope difference over the pickup footprint, normalized by
the fundamental at that mount. Radiation can sample the same shape directly.

| Full undercut | First eight ratios | Fundamental node x/L |
| --- | --- | ---: |
| Rosewood | 1, 4, 10, 18.131, 28.053, 39.605, 52.859, 67.829 | 0.175630 |
| Padauk | 1, 4, 9.8, 16.963, 25.172, 34.617, 45.690, 58.489 | 0.194360 |
| Balafon | 1, 4, 9.6, 16.177, 23.704, 32.627, 43.282, 55.585 | 0.199865 |
| Uniform limit | 1, 2.757, 5.404, 8.933, 13.344, 18.638, 24.814, 31.872 | 0.224158 |

The positions above are predictions, not measurements. They intentionally
replace tests that assumed the uniform-beam node was the node of every tuned
bar. The leading targets differ from their fitted values by less than 1.4e-9
in ratio. High modes should not be described as exact real marimba partials:
Euler–Bernoulli theory omits shear, rotary inertia and three-dimensional motion.
The revised mode weights still include prior instrument voicing rather than a
complete absolute force-to-SPL calibration.

## Isolated validation

`Tests/BarModesTest.cpp` checks all three families across keys 12–120,
fundamental preservation, strict mode ordering, signed parity, the physical
fundamental nodes, exact central odd-mode footprint cancellation, and modal
node counts through the transition. Dense integration independently checks
finite-footprint excitation and loss, including edges and narrow contacts.
The uniform limit is checked against analytic free/free eigenfunctions.

| Isolated measurement | Maximum |
| --- | ---: |
| FEM refinement, 128 to 256 elements: frequency ratio difference | 0.004426 cents |
| FEM refinement: absolute shape difference | 1.0191e-5 |
| Depth-grid midpoint vs direct solve: frequency ratio error | 4.8373 cents |
| Depth-grid midpoint vs direct solve: absolute shape error | 0.008257 |
| Runtime uniform-limit vs analytic shape | 3.7085e-6 |
| Signed footprint vs 1,024-point integration | 6.5629e-7 |
| Squared participation vs 1,024-point integration | 1.6107e-6 |

The generator also checks modal mass orthogonality and parity, then verifies
both the numeric fixture and the embedded generator SHA-256. Numeric comparison
allows small LAPACK/ARPACK rounding differences; it does not require identical
last bits across development machines. `--fit` reruns optimization; routine
checks use the frozen fitted geometry. The new constant data is shared across
voices (roughly 640 kB), with no new per-voice simulation state. Integration
CPU, output-level and listening results belong to the full-engine validation,
not these isolated mathematical tests.

```sh
python3 Tests/generate_bar_modes.py
clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP Tests/BarModesTest.cpp -o /tmp/bar-modes
/tmp/bar-modes
```

## Other concrete improvements, ranked

1. **Mallet compression, separation and rebound from a solved interaction**
   (Rosewood: broad yarn/core contact; Padauk: firmer shorter contact).
   Expected benefit: high for touch and rapid repeated strokes. Current
   `MalletContact` dissipates energy reciprocally but the pulse and release time
   are prescribed. A potential `V(delta)=k [delta]+^(alpha+1)/(alpha+1)` and
   discrete-gradient reaction would let bar velocity influence compression,
   separation and secondary contact. DAFx24's convex piecewise-quadratic
   potential gives a bounded segment search and quadratic solution, with no
   iterative audio-rate root finder. CPU is added only while a mallet is near
   contact; stability still requires the modal/mallet energy accounting to be
   integrated and tested together. A standalone conservative contact test is
   insufficient to prove the complete current normalized voice remains passive.

2. **Geometry-based cavity overlap and reactive loading** (Rosewood tubes versus
   Padauk boxes). Expected benefit: medium/high for bloom, detuning and changes
   in resonator distance; CPU: mostly note-on if the coupling matrix is reduced
   offline. The 2023 multimodal formulation identifies the missing geometric
   dependence. The current passive rotations remain useful, but voiced overlap
   constants should eventually come from the actual undercut surface and mouth
   aperture. Positive energy and frequency changes must be tested together.

3. **Measured per-mode loss and mobility fitting for distinct specimens**.
   Expected benefit: high for recognizable material identity, little runtime
   cost. Record isolated bare bars, resonators and assembled notes across
   velocities/positions; fit pole frequencies, decay envelopes and force/readout
   factors with constraints from the beam. Independent held-out notes and
   strikes are essential. Existing Rosewood/Padauk loss and output weights are
   broad voicing choices, and sophisticated geometry does not turn them into
   measured species constants. This work needs reference recordings or measured
   transfer functions that are not supplied by the papers above.

4. **Orthotropic 3D reduction for high-register and torsional modes**.
   Expected benefit: medium, strongest on edge strokes and specific registers;
   runtime cost can remain small by reducing FE modes offline. Requires
   dimensions, grain orientation and measured mode labeling. It is higher
   effort than this pass, and is the route to validating the currently separate
   torsion law rather than adding arbitrary extra resonances.
