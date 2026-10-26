# Physical model evidence and approximation boundaries

Research reviewed on 2026-09-22. This document explains the physical basis of the
realism work and the limits of what the model can claim. The four instrument
names describe voiced instrument families. They are not measurements of four
identified acoustic specimens.

This records the first pass. The [second pass](physical-models-round2.md) adds
directional radiation and replaces the nearest-mode upper-air selection below
with continuous multimode overlap.

## What the research supports

| Mechanism | Evidence | Consequence for this engine |
| --- | --- | --- |
| Two-way bar and resonator interaction | Soares, Debut and Antunes describe and experimentally validate coupled mechanical and acoustic modes. Frequency alignment, damping and coupling strength change radiation and decay. [1, 2] | Resonator coupling should change the evolution of the ringing bar as well as the microphone balance. A louder cavity is not free additional sustain. |
| Spatial modal participation | The bar/resonator experiments find weak interaction at bar nodes; distributed bar models also project forces at their application locations. [2, 3] | Strike position and contact width should select modal participation. A broad contact can average opposite lobes to little net excitation while still dissipating motion under the contact. |
| Physically stable interaction | Energy-based numerical schemes can support nonlinear connected instruments in real time without expensive iterative solves. [4] | Use bounded state exchange and dissipative contacts; retain fixed-size state and bounded processing cost. More feedback is not inherently more realistic. |
| Kalimba bridge geometry | Chapman's measured/modelled tines use clamped–supported–free boundaries, with overtone ratios dependent on the subdivision at the bridge. [5] | The ideal cantilever spectrum is a baseline approximation. It is not a universal exact spectrum for a mounted kalimba tine. |
| Brief inharmonic kalimba attack | Chapman's demonstration describes strong fundamental motion and a short, inharmonic upper attack that settles into a comparatively pure ring. [6] | Preserve the fundamental; keep nail/bridge detail concentrated near the release. A sustained metallic cloud is not required for realism. |
| Nonlinear membrane coloration | A dizi mirliton study compares the membrane's added upper partials and sidebands; thin-plate experiments show level-dependent harmonic enrichment and more complex vibration at greater drive. [7, 8] | Membrane memory, resonant response and level dependence are useful audible mechanisms. These sources support an analogy, not measured balafon constants. |

## The reduced implementation

Three damped acoustic modes exchange normalized pressure and selected
bar/tine velocities through orthogonal state rotations. This gives reciprocal
interaction with bounded numerical energy and avoids an independent scalar
loading term. It is a reduced, voiced coupling model, not a direct discretization
or parameter fit of references [1–2]. The lowest air mode couples to the
bar/tine fundamental; each upper air mode selects the closest bending mode in
log frequency. This sparse choice is an approximation to a full spatial
coupling matrix.

The concert tube uses the closed/open 1 : 3 : 5 pattern. The Padauk box uses
1 : 2.63 : 4.37 and the gourd uses a dominant fundamental with weaker, more lossy
2.81 and 4.62 upper modes. These upper box/gourd ratios and aperture strengths
are voicing choices. The kalimba cavity uses fixed frequencies near 245, 566 and
1022 Hz instead of transposing with each tine. It has separate state per voice,
so it does not include all the feedback paths of a single common physical box.

The mirliton is a pressure-driven oscillator with inertia, damping and positive
cubic hardening stiffness, updated using a discrete-gradient formulation. Its
velocity supplies the radiated buzz and it continues moving briefly after drive
ends. The membrane is driven by the cavity without a return force to it; this
is a nonlinear radiation layer, not a fully reciprocal air/membrane system.

The support/body path uses three model-specific band-pass modes with a
force-to-velocity-shaped transfer response. Its normalized input is mechanical
modal motion, not a calibrated force measurement. The sympathetic path is a
24-mode reduced bank driven through distance weights and model-specific losses.
Its resonances select spectral overlap instead of a musical-interval whitelist;
it remains a one-way approximation to support transmission. Neither path is a
full coupled finite-element model of the frame and keyboard.

## Instrument-specific interpretation

**Imperial Rosewood.** Its tuned undercut-bar spectrum and narrow resonator response
should give a clear pitch, a rounded force-dependent attack and a brief acoustic
bloom. Coupling represents energy transfer and radiation loading. The overtone
weights, decay coefficients, tube Q and register transitions are voiced values.
The model is not an orthotropic finite-element reconstruction of a particular
rosewood bar. The small split component is a voicing approximation; wood grain
alone does not establish the exact near-unison split used here.

**Mayan Padauk.** The audible distinction comes from a firmer contact, stronger
short upper modes and a broader, shorter box response. Box/body modes should add
wooden articulation without behaving like long narrow concert tubes. The listed
material and construction descriptions do not imply that the engine has measured
density, humidity, grain direction, wall thickness or loss factors for that wood.

**Balafon Ancestral.** The bar, gourd response and pressure-driven buzz have separate
roles: pitch, cavity bloom and level-dependent nasal texture. A membrane has
memory and loses energy; it should not behave as an independent noise generator.
Its threshold, stiffness, damping and readout levels remain voiced surrogates.
The dizi and thin-plate sources below do not validate an exact calabash membrane
model. A convincing buzz must also remain distinguishable from clipping in the
preamp or limiter.

**Kalimba Artisan.** Release from initial tine deflection, a short bridge/nail
transient and a persistent fundamental are the useful physical ingredients.
The shared wooden body has its own resonances; they should not all transpose
one-for-one with every tine. The ideal clamped–free modal ratios and spatial
shapes in the basic tine model remain a reduction of the real bridge-supported
geometry. A future measured geometry or offline modal fit can improve that
approximation without requiring a full beam solve in the audio callback. [5, 6]

## Audible evidence and control behavior

The following are engineering acceptance criteria, not conclusions from the
papers:

- Compare dry notes and repeated notes with identical seeds and gain. Each added
  mechanism must produce a reproducible difference in the appropriate part of
  the attack, spectrum or decay. A changed state variable alone is insufficient.
- Coupling and resonator tuning should alter bloom, radiation and decay without
  runaway feedback, a detached whistle or uncontrolled bass accumulation.
- Hardness, mallet/release type, position and velocity should have distinguishable
  effects. A quiet strike must not expose a constant mechanical-noise floor.
- Decay should extend the pitched ring while keeping the contact brief. Artifacts
  should create occasional physical events; Buzz should create membrane texture.
- Compare close, far and pickup outputs before room processing. A change in
  pickup loading must not change the acoustic frame excitation.
- Check low, middle and high registers, chords, restrikes, automation and all
  supported rate/quality combinations. Listen as well as measuring peaks,
  residuals, spectra and decay.

Fixed-size modal reduction is an appropriate real-time choice here. Modern
differentiable modal methods offer an offline route to fitting recordings, but
the cited example trains on synthetic objects; it does not supply calibrated
Maremba instrument data. [9] Neither more equations nor a current publication
date establishes perceptual superiority. Maximum realism and universal
pleasantness cannot be established without reference recordings, controlled
listening and measurements of the intended instruments.

## Primary references

1. Filipe Soares, Vincent Debut and José Antunes (2023), **The bar-resonator
   interaction in mallet percussion instruments: A multi-modal model and
   experimental validation**, *Journal of Sound and Vibration* 548, 117528.
   [Publisher abstract](https://www.sciencedirect.com/science/article/pii/S0022460X22007118),
   [DOI](https://doi.org/10.1016/j.jsv.2022.117528).
   Supports reciprocal modal coupling; the published apparatus is not a
   calibration of the four instruments in this product.

2. Filipe Soares, Vincent Debut and José Antunes (2022), **The vibroacoustic
   interaction in mallet percussion instruments: Modelling and experiments**,
   TecniAcústica Elche.
   [Full paper](https://documentacion.sea-acustica.es/publicaciones/Elche22/ID-187.pdf).
   Accessible earlier formulation and experimental discussion of tuning,
   distance, spatial coupling and the radiation/decay tradeoff.

3. Stefan Bilbao (2009), **A modular percussion synthesis environment**, DAFx-09.
   [Full paper](https://www.dafx.de/paper-archive/2009/papers/paper_68.pdf).
   Distributed bars/plates, spatial excitation, frequency-dependent loss and
   nonlinear connections; a general numerical framework, not measured Maremba
   constants.

4. Stefan Bilbao, Michele Ducceschi and Craig Webb (2019), **Large-scale real-time
   modular physical modeling sound synthesis**, DAFx-19.
   [Paper and download](https://dafx.de/paper-archive/details/glxmTJxjLpMNMY2RNWrxEg).
   Supports energy-aware stable interaction and bounded real-time computation.
   Maremba's reduced implementation is not a reproduction of that full solver.

5. David M. F. Chapman (2012), **The tones of the kalimba (African thumb piano)**,
   *Journal of the Acoustical Society of America* 131(1), 945–950.
   [Indexed abstract](https://pubmed.ncbi.nlm.nih.gov/22280717/),
   [DOI](https://doi.org/10.1121/1.3651090).
   Measured tine spectra and a clamped–supported–free beam model.

6. David M. F. Chapman (2008), **The sound of the African thumb piano (kalimba)**,
   Acoustical Society of America, author's demonstration of paper 5aMUf1.
   [Article and sound examples](https://acoustics.org/pressroom/httpdocs/155th/chapman.htm).
   Describes one Hugh Tracey pentatonic instrument; its reported overtone range
   is not a universal material constant.

7. Simon Linke, Rolf Bader and Robert Mores (2019), **The impulse pattern
   formulation (IPF) as a nonlinear model of musical instruments**, ISMA 2019.
   [Full paper](https://pub.dega-akustik.de/ISMA2019/data/articles/000056.pdf).
   Section 3 discusses dizi membrane coloration. Maremba does not implement IPF;
   this reference informs the audible target of a driven mirliton only.

8. Alexis Mousseau, Soizic Terrien and Vincent Tournat (2025), **Experimental study
   of the acoustic frequency-up conversion effect by nonlinear thin plates**.
   [Author preprint](https://arxiv.org/abs/2509.12785).
   Laboratory evidence for amplitude-dependent nonlinear acoustic coloration;
   thin clamped plates are not a measured balafon membrane.

9. Rodrigo Diaz, Ben Hayes, Charalampos Saitis, György Fazekas and Mark Sandler
   (2022), **Rigid-body sound synthesis with differentiable modal resonators**.
   [Author preprint](https://arxiv.org/abs/2210.15306).
   An example of offline data-driven modal identification, not a component
   silently used by this engine.
