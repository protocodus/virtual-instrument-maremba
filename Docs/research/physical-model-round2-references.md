# Second physical-model review

Reviewed on 2026-09-22. These are primary research findings, implementation
decisions, and explicit approximation boundaries. None of the sources supplies
measured parameters for all four Maremba instruments. Numerical tests establish
the contracts described below; they do not establish listening preference or
maximum realism.

## Evidence that changes the implementation

**Radiation depends on where the modal motion is observed.** Bestle, Eberhard and
Hanss synthesize idiophone sound from surface vibration and validate their
vibraphone example against boundary-element predictions and microphone
measurements. Their abstract supports the modelling direction, not Maremba's
specific quadrature or constants. [1] Pavill and Shepherd's 2025 measurements
show that mounting changes a glockenspiel bar's directivity, especially for
higher modes. This is evidence against treating every mode as the same point
source; it is also a warning that a bare beam is not a complete mounted
instrument. These measurements are of glockenspiel, not rosewood or kalimba. [2]

**The marimba's tuned 1:4:10 modes are successive vertical bending modes.**
Beaton and Scavone explicitly identify them as the first, second and third
vertical-flexural modes. They are not three successive *symmetric* bending
modes with omitted antisymmetric modes between them. Keeping alternating
symmetry in the approximate spatial basis is therefore appropriate. The exact
shapes still depend on the undercut, and measured bars can also contain nearby
torsional/lateral modes. Adding arbitrary near-unison oscillators is not a
substitute for that geometry. [3, 4]

**Air modes couple through a spatial matrix, not a nearest-frequency switch.**
Soares, Debut and Antunes model multiple bar and acoustic modes together.
Coupling depends on placement, separation and mode shapes; resonance frequency
and loss affect the resulting transfer. [5] Maremba's continuous two-neighbour
upper-air aperture is a bounded reduction of that idea. Log-frequency
interpolation, smoothstep, loss weighting and normalization are engineering
choices, not coefficients fitted from the paper. The shared rank-one rotation
preserves the existing reciprocal exchange invariant. It avoids the previous
discrete change of which tine mode feeds a fixed kalimba cavity at neighbouring
keys. The fundamental air mode still couples to the fundamental bar mode.

## Bounded spatial readout

`DSP/ModalRadiation.h` provides a relative complex transfer for each upper
bending mode and two fixed virtual microphone positions. It integrates signed
modal displacement over a rectangular top face:

```text
H_m = sum_j [a_j phi_m(x_j) exp(-i k (R_j-R_c)) / R_j]
      / sum_j [a_j |phi_m(x_j)| / R_j]
k = 2 pi f_m / 343
C_m = (1-s) + s H_m
```

`R_j` is the distance from surface point `j` to the microphone, `R_c` the
distance from the surface centre, and `a_j` the positive quadrature weight.
The numerator is the spatial part of a Rayleigh/point-source approximation;
the denominator is our explicit bounded normalization. It removes absolute
level and makes `|H_m| <= 1`. It retains opposite-lobe cancellation, relative
propagation phase and the variation of distance across an extended source.
It does not predict calibrated pressure or radiation damping.

The blend strength `s` acknowledges that existing modal amplitudes are already
voiced acoustic readouts and the uniform-beam shapes only approximate an
undercut bar. No second frequency-squared radiation gain is applied. With
`0 <= s <= .4`, the new coefficient magnitude cannot exceed one or fall below
`.2`. The fundamental is explicitly set to `1 + 0i` as a calibration reference,
preserving its previous level and phase. This exception and the blend are
voicing decisions; they are not discoveries about beam radiation.

| Instrument | Length at C4 | Width at C4 | Close / far correction strength | Intended audible consequence |
| --- | ---: | ---: | ---: | --- |
| Imperial Rosewood | .350 m | .055 m | .16 / .34 | Retain pitched ring; more local upper-mode articulation close to the bar and less upper-mode accumulation in the far feed. |
| Mayan Padauk | .320 m | .050 m | .18 / .36 | Keep the short wooden attack distinct while reducing the same upper modes' far-field prominence. |
| Balafon Ancestral | .330 m | .057 m | .16 / .34 | Give the bar its own spatial coloration, separate from gourd and membrane readouts. |
| Kalimba Artisan | .055 m | .007 m | .12 / .28 | A modest change in the short inharmonic attack; preserve the fundamental and independent box response. |

The rosewood length scale gives about .416 m at F#3, within the .373–.423 m
span reported for the measured F#3 bars in [3]. This checks an order of
magnitude, not a fit. The other dimensions, observation positions and strengths
are voiced assumptions. Length scales approximately as `1/sqrt(f0)`, the
fixed-thickness uniform-beam relationship, with practical limits. Real undercuts
and supports change that law. The close microphone is offset along the bar and
near the playing surface; the far microphone has a greater height and lateral
offset. These are internal model coordinates, not new user controls.

For `y=A cos(phi)` and `q=A sin(phi)`, the existing recurrence supplies
`q=(r*y2-y1*cos(w))/sin(w)`. The readout is
`Re(C)*y - Im(C)*q`. Thus each mode needs only cached real coefficients and
multiply/add operations during rendering. Coordinates must refer to the same
instant and damping state, including a restrike's residual. The geometry and
frequency projection are latched at the strike; pitch bending a ringing voice
can retain that spatial readout as a small-bend approximation.

This is a harmonic, top-face radiation surrogate. It omits the underside,
edge diffraction, frame scattering, finite arrival fronts and exact
mounted-instrument boundary conditions. Removing common flight time avoids
device latency. It is intended only for acoustic microphone signals; it must
not alter the pickup, mechanical support drive or stored modal energy.

## Quadrature, generation and validation

Production uses 32 Gauss-Legendre samples along the length and two Gaussian
samples across the width. The signed bar/tine shapes are frozen `constexpr`
values, so triggering does not repeatedly calculate beam exponentials and
trigonometry. The table can be regenerated as follows:

1. Obtain `z,w = numpy.polynomial.legendre.leggauss(32)`.
2. Map to the unit beam with `x=(z+1)/2`, `weight=w/2`.
3. At each `x`, evaluate `ModalContactWeight(m,x,0,false)` and
   `ModalContactWeight(m,x,0,true)` for `m=0..7`, and print with 17 significant
   digits into `kModalRadiationNodes`.

`Tests/ModalRadiationTest.cpp` checks every frozen shape against the excitation
formula. It also checks all four instruments over four registers, finite
bounded output, preservation of the fundamental, reflection parity, cancellation
of an antisymmetric mode from a symmetric observation point, restoration off
axis, and modal-balance changes with distance. The recurrence-coordinate check
covers 44.1–384 kHz without an output-level normalization that could hide a
rate-dependent error.

In the initial helper check, the largest complex-field error against a
2048-point longitudinal reference was .01160 over the tested modes below
20 kHz, before multiplying by a correction strength of at most .36. The largest
same-state sample-rate discrepancy was `1.11e-16`. The dense reference checks
longitudinal convergence; it shares the two-point width approximation.
Ultrasonic directivity is only bounded, not validated against that reference.
A local optimized C++ benchmark of 4000 varying triggers, all eight modes,
measured 7.2–11.4 microseconds per trigger. This is a helper measurement on the
development machine, not a Rack Extension deadline or platform guarantee.

Voice integration still needs an acoustic ablation: identical deterministic
strikes with spatial coefficients enabled versus unity, separately for close
and far. Check change in the first 100 ms and upper-mode-to-fundamental balance,
plus identical isolated pickup and mechanical outputs. Chords, restrikes,
pitch bend and soft velocities should remain smooth. A measurable residual
does not by itself prove that the change is audible or pleasant.

## Valuable remaining work, not implemented by this helper

**Bridge-supported kalimba eigenmodes.** Chapman validates a
clamped–supported–free Euler–Bernoulli tine model against measured frequencies.
Unlike a cantilever, its ratios vary with the position of the intermediate
bridge. The present fixed `1:6.267:17.550:...` spectrum remains an approximation.
A bounded next step is an offline eigenmode table, retaining the tuned
fundamental while varying the upper ratios and contact shapes together. [6]

One derivation uses two beam segments, each with
`phi=A cosh(beta x)+B sinh(beta x)+C cos(beta x)+D sin(beta x)`.
At the clamp require zero displacement and slope; at the free end require
zero curvature and shear. At the internal support both sides have zero
displacement, with continuous slope and bending moment. Shear may jump because
the support supplies a reaction. The determinant gives the modal roots and
frequency ratios `(beta_m/beta_0)^2`. This boundary-equation outline is our
proposed implementation route, not a copied numerical fit from Chapman.
Track mode identity as bridge position changes and normalize modal mass before
deriving pluck participation. Validation should reproduce the cantilever limit,
compare a refined beam discretization, and check continuous spectra and
strike-position response. The intended change is a register-dependent upper
attack, not random detuning of the sustained pitch.

**State-dependent mallet collision.** The existing pulse already models finite
duration, spatial contact and force-dependent brightness. Replacing it with a
dynamic mallet/contact solve is justified only if it yields reproducible
ringing-phase-dependent restrikes, rebound or hysteretic loss. A candidate
would evolve mallet displacement and integrate a passive unilateral contact
potential against the bar's contact displacement. Merely adding another
envelope or arbitrary noise does not establish that mechanism. This round
does not implement or claim a validated viscoelastic collision solver.

## Primary sources

1. Pascal Bestle, Peter Eberhard and Michael Hanss (2017), **Musical instruments
   – Sound synthesis of virtual idiophones**, *Journal of Sound and Vibration*
   395, 187–200. [DOI](https://doi.org/10.1016/j.jsv.2017.02.010),
   [author's institutional publication list](https://www.itm.uni-stuttgart.de/institut/ehemalige_mitarbeitende/homepages/bestle/).
   The abstract was available; Maremba does not claim to reproduce its full
   algorithm or experimental parameter set.

2. Hanna M. Pavill and Micah R. Shepherd (2025), **The effect of mounting
   conditions on the vibration and directivity patterns of the glockenspiel**,
   *JASA Express Letters* 5, 093201.
   [DOI](https://doi.org/10.1121/10.0039258),
   [indexed primary abstract](https://pubmed.ncbi.nlm.nih.gov/40990930/).
   Related author thesis:
   [Directivity and Sound Radiation of Percussion Instruments and Harmonic Analysis of the Alto Saxophone](https://scholarsarchive.byu.edu/etd/10930/).

3. Douglas Beaton and Gary Scavone (2019), **Measurement-based comparison of
   marimba bar modal behaviour**, ISMA 2019, 84–89.
   [Primary full paper](https://pub.dega-akustik.de/ISMA2019/data/articles/000040.pdf).

4. Douglas Beaton and Gary Scavone (2021), **Three-dimensional tuning of
   idiophone bar modes via finite element analysis**, *JASA* 149, 3758–3768.
   [Author-hosted full paper](https://caml.music.mcgill.ca/lib/exe/fetch.php?media=publications:beaton_idiophonebartuning_jasa_2021.pdf),
   [DOI](https://doi.org/10.1121/10.0005062).

5. Filipe Soares, Vincent Debut and José Antunes (2023), **The bar-resonator
   interaction in mallet percussion instruments: A multi-modal model and
   experimental validation**, *Journal of Sound and Vibration* 548, 117528.
   [Publisher](https://www.sciencedirect.com/science/article/pii/S0022460X22007118),
   [DOI](https://doi.org/10.1016/j.jsv.2022.117528).

6. David M. F. Chapman (2012), **The tones of the kalimba (African thumb piano)**,
   *JASA* 131, 945–950.
   [Indexed primary abstract](https://pubmed.ncbi.nlm.nih.gov/22280717/),
   [DOI](https://doi.org/10.1121/1.3651090),
   [author's earlier demonstration and audio](https://acoustics.org/pressroom/httpdocs/155th/chapman.htm).
