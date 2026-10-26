# Membrane material stretching

Research and implementation reviewed on 2026-09-24. This adds geometric
stretching to the existing slack membrane; it does not replace the gourd with
a measured specimen or claim a fully reciprocal air/membrane model.

## Evidence and remaining opportunities

| Finding | Source and scope | Consequence |
| --- | --- | --- |
| Gyil gourds can carry several buzzing membranes; their acoustic response is more broadband than the paper's single nonlinear approximation. | Godlovitch, Trail, Tavares and Tzanetakis (2012), [Physical Modeling and Hybrid Synthesis for the Gyil African Xylophone](https://webhome.csc.uvic.ca/~gtzan/smc2012model.pdf), primary instrument/model study. | A future second membrane with a different threshold could broaden the buzz, at the cost of another nonlinear state update per active voice. This pass keeps one membrane. |
| A reduced thin-membrane model contains cubic elastic force and a positive loss proportional to displacement squared times velocity. | Bellet, Cochelin, Côte and Mattei (2014), [Enhancing the Dynamic Range of Targeted Energy Transfer in Acoustics Using Several Nonlinear Membrane Absorbers](https://arxiv.org/html/1407.4904), section 2.1, equations of motion. The membrane force includes `k3*(q³ + 2*eta*q²*qdot)`, with `k3 = 8*pi*E*h / (3*(1-nu²)*R²)`. Laboratory absorber membranes are not gyil measurements. | Implement positive cubic stretching and strain-dependent loss after the wrinkles are exhausted. The material parameters remain voiced. |
| A membrane has a reactive impedance as well as damping; attaching it can lower and broaden the resonator's admittance peaks. | Luan, Wang, Scavone and Li, [Acoustic Characterization of the Resonator in the Chinese Transverse Flute (dizi)](https://arxiv.org/html/2409.04853v2), 2025 revision, equation 12 and section IV.2. This is small-signal dizi data, not a gourd calibration. | The existing one-way pressure drive plus slap-energy drain still lacks reciprocal reactive loading. A future coupled solve needs a common physical energy normalization and calibration; adding an arbitrary return force would not establish fidelity. |

The first two larger opportunities are ranked separately from this change:
reactive air/membrane coupling has high physical value but substantial stability
and calibration cost; multiple staggered membranes offer a direct construction
analogy and likely timbral benefit but extra per-sample CPU. Material stretching
is a bounded first step with no additional oscillator, delay, random modulation,
or per-sample transcendental function.

## Implemented law

For each side, `s = max(signed displacement - gap, 0)`. Its additional elastic
potential is

```
V(s) = stiffness*s²/2 + stiffness*stretch*s⁴/(4*gap²).
```

The quartic potential is nonnegative and convex. A larger taut excursion now
increases the spring's tangent stiffness, so its resonance changes with drive
and relaxes as the vibration dies. The pre-existing slack response and snap
threshold remain. The shadow membrane is still linear.

`SetMaterial(stretch, strainLoss)` accepts nonnegative dimensionless voicing
values. Stretch is the ratio of cubic to linear additional spring force at an
overshoot of one gap. The extra damping ratio is
`strainLoss*(s/gap)²`, added before the existing loss cap. Its coefficient is
held constant during each integration step, so it cannot supply energy.
Reciprocals of the gaps are folded into cached coefficients at configuration.

The integrated Balafon setting is `(0.30, 0.15)`; the heavier Padauk
charleo uses `(0.12, 0.08)`. These are engineering voicings, not values inferred
from membrane density, thickness, radius, or species measurements. Zero
material preserves the original numerical path.

The discrete gradient uses factorized divided differences of the unilateral
quadratic/quartic potentials, including exact slack-limit crossings. Newton
steps are safeguarded by a bracket derived from the residual's positive lower
slope bound, with at most 48 iterations. The old eight-step solver remains only
on the unchanged zero-stretch branch. The linear slack path has no solve.
The bounded solver is a numerical method used here, not copied from the papers.

## Focused validation

`Tests/SlackMembraneStretchTest.cpp` checks the law independently of presets and
microphone gains. Built with Clang C++17, `-O3 -Wall -Wextra -Werror`:

- 80,000 samples compare bit for bit against the frozen previous implementation
  when both material values are zero, including forcing and hand damping.
- 864,000 forced/free steps at 22.05, 44.1, 48, 96, 192 and 768 kHz, three
  stretch settings and four excursion scales check pressure work, positive
  loss, and passive release. The independent long-double energy calculation
  found maximum relative work-balance error `6.69e-13`; passive growth was zero.
- Against a 705.6 kHz reference, displacement RMS error decreases from 0.902%
  at 44.1 kHz to 0.407% at 88.2 kHz and 0.172% at 176.4 kHz.
- A below-gap free ring stays at 383.3 Hz. At four gaps, the ideal low-loss
  test's mean oscillation frequency changes from 973.5 Hz without stretching
  to 1557.8 Hz with stretch 0.3. This is a mechanism check, not a target gyil
  spectral measurement.
- Address and undefined-behavior sanitizer runs pass the same contract.

Raw logs are in `Release/validation/physical-models-round5/membrane/`.
Whole-instrument audio, all existing buzz/rate/voice contracts, and default
quality CPU measurements are separate integration gates; the kernel tests do
not replace them. This change intentionally alters the sound when stretching
is enabled and does not imply a released U45.

`Round5ResponseTest` additionally compares production voices with copies whose
material coefficients alone are zeroed, preserving their geometry, random
sequence and initial states. At full Buzz and forte, Padauk's largest waveform
difference after fitting gain is 0.928%, and Balafon's is 6.756%. After removing
both phase and overall gain by comparing normalized spectral magnitudes, the
largest differences are 0.173% and 1.867%, respectively. Whole-note RMS changes
by at most 0.255 dB after per-key force calibration. These measurements prove a timbral effect reaches actual
voice audio; they do not measure perceptual preference. At Buzz zero, all three
voice outputs remain bit-identical in the comparison. The whole-voice contract
also passes address/undefined-behavior sanitizers. Current calibrated integration logs are in
`Release/validation/physical-models-round5-20260924/`; the earlier uncalibrated
measurements remain in the membrane subdirectory listed above.
