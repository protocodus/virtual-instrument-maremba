# Bridge-supported kalimba model: evidence and derivation

Reviewed on 2026-09-22. The new tine geometry replaces a uniform cantilever
approximation with a uniform beam clamped behind an intermediate bridge and
free at the playing end. Its frequencies, contact shapes and spatial radiation
now come from the same boundary problem. It remains a reduced, voiced
instrument family, not a measured reconstruction of a particular kalimba.

## Primary evidence and parameter choice

Chapman's 2012 paper reports acoustic measurements and a
clamped–supported–free Euler–Bernoulli model, with frequency ratios depending
on the bridge location. The abstract reports agreement between calculated and
measured frequencies. The complete 2012 article was not accessible in this
review; its detailed equations have not been presumed. [1]

His accessible 2006 paper measures free playing lengths of 3.7–5.4 cm and
bridge-to-restraint spans of 1.3–1.4 cm for one small instrument. Their ratios
are approximately .26–.35. [2] Maremba chooses a representative **rear/free
length ratio of .30**, fixed across keys. It is a plausible geometry within
those measured proportions, not a fit of that specimen's overtone frequencies.
That early study and the later author's demonstration describe different
overtone ranges, so neither is treated as a universal target. [2, 3]

Hong, Dodson, Laflamme and Downey give the full two-span beam boundary
conditions in equations 6–13 of their 2019 paper. Setting its attached tip
mass to zero gives the boundary problem needed here: clamp displacement and
slope vanish; the free end has zero bending moment and shear; bridge
displacement vanishes on both sides while slope and moment are continuous.
Their engineering testbed is not a kalimba calibration. [4]

## Independent analytical reduction

Use free playing length `a=1`, rear length `b=s=.30`, and dimensionless
wavenumber `k=beta*a`. On the rear segment, coordinate `v` runs from the clamp
to the bridge. On the playing segment, coordinate `u` runs from the free end
back to the bridge. Functions that automatically satisfy the outer boundary
conditions are

```text
rear(v) = A [cosh(kv)-cos(kv)] + B [sinh(kv)-sin(kv)]
free(u) = C [cosh(ku)+cos(ku)] + D [sinh(ku)+sin(ku)]
```

Set both displacements at the bridge to zero. The remaining slope and moment
matching yields this scaled determinant, derived for this implementation:

```text
F(k,s) = [cos(ks)-sech(ks)] [sin(k)-tanh(k)cos(k)]
       - [tanh(ks)cos(ks)-sin(ks)] [sech(k)+cos(k)] = 0
```

The shear need not be continuous: its jump is the bridge reaction. Requiring
zero moment at this *internal* support would incorrectly turn it into a hinge
between two separate beams.

`Tests/generate_kalimba_modes.py` brackets the first eight positive roots and
refines them with 70-digit arithmetic. Frequency ratios are `(k_m/k_0)^2`, so
the fundamental remains exactly one:

| Mode | `beta*a` | Frequency / fundamental |
| ---: | ---: | ---: |
| 1 | 1.755320752893 | 1.000000000000 |
| 2 | 4.448899041199 | 6.423801698990 |
| 3 | 7.495719083512 | 18.235330099724 |
| 4 | 10.512563366321 | 35.867761912486 |
| 5 | 13.287837969789 | 57.305416395512 |
| 6 | 14.785514310982 | 70.951224819724 |
| 7 | 17.057170785429 | 94.428049889868 |
| 8 | 20.009366097899 | 129.943238327807 |

These are calculated values, not measured material coefficients. Higher modes
include significant rear-segment motion; their amplitudes must therefore not
be normalized independently to an arbitrary free-end value.

Each raw shape has unit integrated squared displacement over **both** spans,
including the material behind the bridge. It is stored as a sum of two scaled
exponentials, a sine and a cosine on each segment. This avoids subtracting
large hyperbolic terms. No eigenvalue solve runs in the audio callback.

## Contact, release and radiation consistency

`DSP/KalimbaModes.h` supplies the ratios and signed contact weights. Contact
position means a fraction of the exposed bridge-to-tip span; the existing
voice mapping `.65+.35*Position` still describes the region reached by a thumb.
The finite footprint is integrated analytically. Contact dissipation continues
to use the mean of squared displacement, so a footprint spanning a modal node
can cancel net force without pretending that no material was touched.

The fundamental projection is calibrated to one at position `.825` with a
point contact, matching the earlier neutral response. Higher modes retain an
RMS spatial scale of `.5` over the entire two-span beam. These scale choices
are transparent level calibration; eigenfrequencies and shapes are derived.
Existing release-time laws and `kTineReadout/ratio` amplitudes remain voiced
approximations rather than measured bridge impedance or absolute acoustic
pressure. Static pluck participation still uses the derived contact location.

The frozen tine entries in `DSP/ModalRadiation.h` are regenerated from these
same exposed-span shapes. Its bar entries are unchanged. Radiation retains
the bounded correction and fundamental calibration described in the
[second-round references](physical-model-round2-references.md). It does not
model acoustic radiation from the small rear span separately.

The predicted audible change is the brief inharmonic attack and its response
to where the tine is released. The first overtone moves from the cantilever's
approximately 6.267 to 6.424 times the fundamental, while the sustained pitch
stays fixed. Changed modal nodes affect release participation and the spatial
readout coherently. This is a specific spectral prediction, not a claim that
listeners have preferred the new sound.

## Reproduction and independent checks

Run the generator without arguments to verify the committed table and radiation
cache. `--write` regenerates only the derived mode table and tine cache.
Development dependencies are `mpmath`, `numpy` and `scipy`; none enters the
Rack Extension.

```sh
python3 Tests/generate_kalimba_modes.py
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I DSP Tests/KalimbaModesTest.cpp -o /tmp/kalimba-modes-test
/tmp/kalimba-modes-test
```

The generator separately assembles cubic-Hermite Euler–Bernoulli stiffness and
consistent-mass matrices. The clamp removes displacement and rotation; the
bridge removes displacement only. A generalized shift-invert eigensolve then
supplies an independent reference. With 100 elements over the playing span
and 30 behind the bridge, the largest relative wavenumber difference from the
analytical roots was `5.56e-7`. Refining to 200+60 elements reduced it to
`3.48e-8`. This validates the numerical boundary problem, not the realism of
the assumed instrument dimensions.

The C++ test checks the independent FEM roots, all outer and bridge conditions,
modal mass and orthogonality over both spans, exact finite-footprint integrals,
position dependence, and positive contact participation at a displacement
node. It also checks two independent limiting cases: shrinking the rear span
recovers cantilever ratios; equal spans yield the analytical root families
`cos(k)=0` and `tan(k)=tanh(k)`.

Observed maximum errors in the initial strict build were `1.91e-15` for scaled
boundary residuals, `1.18e-11` for mass/orthogonality, and `2.89e-14` for contact
integration. The updated radiation test passes its dense spatial-reference,
parity, boundedness and sample-rate checks. A 100 ms, three-mode pluck proxy
with unchanged fundamental and damping produced a difference of −23.70 dB
relative to the cantilever reference. That proxy establishes a non-negligible
change in a rendered signal; it is not a full-engine listening assessment.

## Limits of the instrument representation

The fixed support proportion omits actual key-to-key mounting differences,
compliant bridge motion, clamp friction, tine curvature, torsion, shear
deformation and rotary inertia. The wooden soundbox remains a reduced model;
there is no newly derived bridge-to-box mechanical impedance in this helper.
An exact specimen fit would need its dimensions and measured responses.

The broader engine uses a reduced bank of 61 virtual chromatic sympathetic
receivers over C2–C7, with a bounded set of nearby receivers driven by each
voice. This is an extended playable synthesis approximation. A “17-key”
kalimba family name does not mean that these 61 virtual receivers represent
61 physical tines on a measured instrument. The bank remains a one-way
support-response approximation, not a complete reciprocal keyboard.

## Motion-dependent mallet contact on the wooden instruments

`DSP/MalletContact.h` adds a finite-mass correction during a restrike. Existing
modal velocity drives a normalized mallet-head velocity through a two-point
contact footprint. A coherent port exchanges and dissipates motion, while a
footprint-difference port absorbs motion when a wide head spans a node. This
creates phase-dependent absorption and spatial transfer between previously
ringing modes. Its contact mobilities reuse the instrument's voiced modal
amplitudes. The effective head mass is a normalized synthesis choice.

Finite mass, contact duration and viscoelastic loss affect impact spectra in
physical contact models. [5] Numerical energy accounting is useful because
stable continuous contact laws do not automatically give stable discretizations.
[6] Here each rank-one velocity update is an exact dissipative subflow;
symmetric splitting preserves a non-increasing sum of squared modal and head
velocities. The finite contact envelope supplies the integrated time step.
This is an explicit reduced approximation, not an implementation of either
paper's complete nonlinear impact solver.

The original fresh-strike pulse and nominal separation time remain prescribed.
The new path corrects the old ringing component only. It does not derive Hertz
compression, solve nonlinear release, or claim measured mallet masses. Local
passivity is not a whole-voice energy-conservation claim.

`Tests/MalletContactTest.cpp` independently checks local passivity, single-mode
node protection, finite-footprint absorption, spatial transfer, head state
lifetime and public-audio ablation. Fresh hits are bit exact with the correction
disabled. Across 16 restrike phases and all three wooden models, measured
waveform differences are -28.9 to -5.43 dB relative to the former contact path,
with changes beyond a simple gain scaling. Isolated correction levels differ
by at most .0022 dB across 88.2, 96 and 192 kHz in the tested case.
The [contact validation notes](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Release/validation/physical-models-round3-20260922/mallet-contact-notes.md)
record the exact algorithm, evidence, limitations and why one mixed-mode
absorption assertion was replaced by a pure-mode test.

## Sources

1. David M. F. Chapman (2012), **The tones of the kalimba (African thumb piano)**,
   *JASA* 131, 945–950.
   [Primary abstract](https://pubmed.ncbi.nlm.nih.gov/22280717/),
   [DOI](https://doi.org/10.1121/1.3651090).

2. David M. F. Chapman (2006), **Investigating the Sound of an African Thumb
   Piano (Kalimba)**, *Canadian Acoustics* 34(3), 26–27.
   [Primary full paper](https://jcaa.caa-aca.ca/index.php/jcaa/article/download/1805/1552/1942).
   Table 1 supplies specimen lengths; its early theoretical model was still
   under development and is not treated as the final 2012 result.

3. David M. F. Chapman (2008), **The Sound of the African Thumb Piano (kalimba)**,
   author's Acoustics'08 demonstration of paper 5aMUf1.
   [Primary article and audio](https://acoustics.org/pressroom/httpdocs/155th/chapman.htm).

4. Jonathan Hong, Jacob Dodson, Simon Laflamme and Austin Downey (2019),
   **Transverse Vibration of Clamped-Pinned-Free Beam with Mass at Free End**,
   *Applied Sciences* 9, 2996.
   [Author-hosted full paper](https://faculty.sites.iastate.edu/laflamme/files/inline-files/Laflamme_J70_2019.pdf),
   [DOI](https://doi.org/10.3390/app9152996).

5. Federico Avanzini and Davide Rocchesso (2004), **Physical Modeling of Impacts:
   Theory and Experiments on Contact Time and Spectral Centroid**.
   [Author-hosted full paper](https://avanzini.di.unimi.it/downloads/publications/avanzini_smc04_finalsub.pdf).

6. Vasileios Chatziioannou and Maarten van Walstijn (2015), **Energy conserving
   schemes for the simulation of musical instrument contact dynamics**,
   *Journal of Sound and Vibration* 339, 262–279.
   [Primary preprint](https://arxiv.org/abs/1501.01493),
   [DOI](https://doi.org/10.1016/j.jsv.2014.11.017).
