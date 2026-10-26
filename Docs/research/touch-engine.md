# Maremba 1.2: touch, contact and release

Version 1.2 intentionally changes the sound of existing patches while retaining device identity, parameter IDs, ranges, automation paths and device defaults. The changes concern how a strike excites and damps the instrument. They are physical approximations with voiced constants, not measurements of a particular mallet, bar or kalimba. The [1.1 engine notes](engine-realism.md) describe the preceding spatial-mode update; [touch-response measurements](touch-response.md) provide the separate numerical and audio evidence for this update.

## One touch controls force and noise

Let `v` denote velocity after the existing selectable MIDI velocity curve and its soft floor. The common impact strength is `v^1.25`; this is a musical mapping, not a calibrated velocity in metres per second. Core compression adjusts the effective hardness around the previous 0.7-velocity voicing point. The adjustment is stronger for mallets than for thumb release. Each striker retains its own duration and material weighting.

Previously the main pulse integral scaled approximately as `v^1.25`, while clack amplitude scaled as `v^0.8`. Friction was added separately with a velocity-independent floor. That made their relative contribution disproportionately large on quiet notes. Clack now follows the common strength multiplied by an additional `v^0.55` accent factor and effective hardness. Friction is a bounded modulation of the contact pulse itself; it cannot remain audible when its driving contact tends to zero. Its filtered noise density is normalized to the internal sample rate.

This choice follows the experimentally established connection between blow strength and a mallet's excitation spectrum, without importing purported measured constants. [Bork, *Measuring the acoustical properties of mallets* (1990)](https://www.sciencedirect.com/science/article/abs/pii/0003682X9090044U) measured how residual shock spectra change with blow strength for different mallets.

## Finite, positive contact

The main contact or thumb-release pulse is integrated over each internal sample. Its cumulative function, for normalized time `0 <= t <= 1`, is:

```text
C(t, k) = 10 t³ - 15 t⁴ + 6 t⁵ + 10 k t³(1 - t)³
C'(t, k) = 30 t²(1 - t)² [1 + k(1 - 2t)]
sample contribution = integral × [C(t_next, k) - C(t_now, k)]
```

For `|k| <= 1`, the force is nonnegative, and both force and its slope reach zero at the endpoints. `k` controls a small material-dependent asymmetry; strike variation can perturb it. Without friction modulation, the sample contributions telescope to the specified integral, up to floating-point precision. A final nonnegative clamp removes possible subtraction roundoff near the endpoints. A hard mallet's short positive core component uses part of the same impulse budget. It is not additional unconstrained energy. The old negative post-contact "rebound" force is removed: a separated, nonadhesive mallet cannot pull the bar toward itself.

The integral includes the common impact strength, effective hardness and a striker-specific voicing coefficient. This is a prescribed contact pulse. It does **not** solve the mallet's mass, compression, separation and repeated collisions with the moving bar. For comparison, [Bilbao, Torin and Chatziioannou (2014)](https://arxiv.org/abs/1405.2589) present energy-based numerical collision models; those nonlinear solvers are outside the present implementation.

## Treble notes retain a touch response

Each material's initial duration `T_raw` shortens with increasing velocity. The old hard limit `min(T_raw, 0.8/f0)` could give exactly the same contact duration at every velocity on a treble note. Its replacement is:

```text
T_limit = (0.8 / f0) × (1.10 - 0.25 v)
T = T_raw / [1 + (T_raw / T_limit)^4]^(1/4)
```

The transition is smooth, and its limiting duration still responds to velocity. Strike variation follows this calculation; a three-internal-sample lower bound protects numerical resolution. Material duration exponents, register limits and core-compression coefficients are voicing laws, not a literal Hertz contact solution. Independent per-mode velocity boosts have been removed: the main spectral response now comes from duration, compression, spatial averaging and the bounded hard-core component.

## Position affects excitation and damping differently

Excitation uses the **signed spatial average** of a mode across the contact footprint. Opposite lobes can cancel, so an ideal centred strike does not drive antisymmetric bar modes. A wider contact removes short spatial wavelengths. The bar footprint grows modestly with touch strength, representing increasing compression area.

Contact damping uses the **average squared mode displacement**, evaluated with two-point Gaussian quadrature. A broad mallet can touch moving material on both sides of a node even when its net signed excitation is zero. Previous-ring damping therefore depends on the mode, contact position, width, striker material, hardness and touch; the old blanket reduction to 25% is gone. Upper modes also use this spatial participation for contact loss during excitation. Both delayed resonator states receive the same attenuation, avoiding artificial energy injection.

This is diagonal, dissipative damping, not a full cross-mode contact matrix. The mode shapes remain uniform free/free beam approximations combined with voiced undercut-bar frequencies. Real undercuts alter nodes and mix additional modes; [Beaton and Scavone's experimental marimba study (2019)](https://pub.dega-akustik.de/ISMA2019/data/articles/000040.pdf) documents that geometric and modal variability.

## Kalimba releases a deflection

The tine uses its clamped/free spatial approximation and the existing playable-end position mapping: `x = 0.65 + 0.35 × position`, with position constrained to 0.05–0.95. Thus the control traverses the outer playable part of the ideal tine, not its full length from the clamp. Its finite thumb-release distribution drives a velocity-like modal response, with no hard impact spike fed into the tine modes.

A mode statically deflected by a local force has displacement proportional to `1/omega²`. Releasing it from rest gives a cosine displacement and a sine velocity; the velocity amplitude is proportional to `1/omega`. The existing sine recurrence can therefore represent a pluck's velocity-like output without abruptly inserting a nonzero displacement into the audio stream. A slower flesh release suppresses upper modes more than a faster nail or pick release.

The explicit `1/frequency_ratio` compliance factor is paired with separately voiced bridge/radiation readout weights. Those weights preserve the intended relative tine partial levels; applying the compliance factor to the already attenuated old weights would count the spectral rolloff twice. They are not measured modal masses or a measured bridge transfer function.

An ideal cantilever is still a simplification. [Chapman, *The tones of the kalimba* (2012)](https://pubmed.ncbi.nlm.nih.gov/22280717/) models a clamped–supported–free tine and finds that overtone ratios depend on the bridge's position. Maremba does not currently solve that support geometry, static preload or body impedance.

## Remaining model boundaries

The tube/gourd remains driven one way by the bar. Resonator detuning changes its response, but does not solve reciprocal energy exchange or the resulting frequency-dependent loading of the bar. The close, far and piezo signals retain their existing voiced transfer weights; they are not geometrically simulated microphones or a calibrated strain pickup. Actual sound radiation depends on surface motion, propagation and observation position, as treated in [Beaton's 2022 dissertation, section 5.3.3](https://caml.music.mcgill.ca/lib/exe/fetch.php?media=publications%3Aphd_beaton_2022.pdf).

The regression suite and response measurements establish the implemented behavior, stability and numerical consistency. They do not establish perceptual indistinguishability from a recorded instrument.
