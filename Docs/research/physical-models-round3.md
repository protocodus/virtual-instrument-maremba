# Third physical-model pass

This pass compares against the completed second pass, frozen before editing in
`Release/validation/physical-models-round3-20260922/sources/before`. It keeps the
1.3.0b4 development identity, controls, defaults and default 2x quality. This is
an intentional sound update; existing patches retain their parameter values.

## Wood-bar contact

Rosewood, Padauk and Balafon now let a ringing bar react against a finite mallet
mass during a restrike. The local contact projects modal velocities onto the
mallet footprint and dissipates relative motion between the bar and mallet.
Oppositely moving parts of a wide footprint also dissipate energy through a
second felt-deformation component. The correction can transfer existing energy
between bending modes, so the result depends on the phase of the ringing bar.

The contact operator uses exact dissipative substeps and includes mallet motion
in its energy accounting. It cannot create energy. Force mobilities reuse the
existing model/material/hardness weights so quiet upper modes are not driven as
strongly as the fundamental. The nominal finite strike pulse remains unchanged.

This is a linearized finite-mass viscoelastic correction around that pulse,
with its existing prescribed separation time. It is not a full nonlinear Hertz
trajectory or a fitted measurement of any particular mallet. Effective mallet
mass and material mobilities are voiced parameters. See the source-backed
[research notes](physical-model-round3-references.md).

## Supported Kalimba tines

Kalimba now solves the clamped–supported–free beam geometry: a short rear span
runs from the clamp to the bridge, and the playing span extends beyond it.
Displacement is zero at the bridge, while slope and bending moment remain
continuous. Both spans contribute to modal mass. Frequencies, contact footprints
and acoustic radiation use the same derived modes.

The representative rear/free length ratio is 0.30. It is dimensionally informed
by published measurements, not a fit to a specific 17-key instrument. The
fundamental remains exactly tuned; the first upper ratios change from about
6.267 and 17.55 to 6.424 and 18.235. Higher modes include motion of the rear span.
Frozen coefficients avoid solving an eigenproblem while playing. The generator
independently checks frequencies with a consistent-mass Hermite beam model.

The model still uses a fixed per-voice soundbox and voiced readout/decay weights.
It omits nonlinear bridge slip, a fully shared box and specimen-specific tine
geometry. The sound should be judged from the comparison audio, not the number
of equations in the implementation.

## Sympathetic response and control continuity

The support bank now covers C2–C7 with 61 receivers. Each source drives at most
24 neighboring keys within one octave and excludes its own receiver. This
removes a parallel copy of the played fundamental, which previously dominated
much of the apparent sympathetic effect. Neighbor resonance now remains useful
across the core register without the old C3/B4 bank-boundary cliffs.

The neighbor-only acoustic gain rises by 6 dB to restore a useful control range;
individual receiver losses and low-frequency rejection remain in place. This
is a reduced one-way support network, not 61 fully reciprocal physical voices.
A virtual extended bank supports transposed Kalimba playing; it does not claim
that the named 17-key specimen contains 61 physical tines. Outside the core
register the available neighboring receivers naturally diminish.

Changing Coupling on a restrike now moves the existing air readout over 5 ms.
New strikes start at their requested value. This prevents a quiet retrigger
from abruptly cutting a still-ringing cavity while retaining the new aperture
coupling and established fresh-strike behavior.

## Validation

The [parameter audit](../validation/round3-parameter-audit.md) records the measured
range, control and transport-stop behavior. The [validation manifest](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/physical-models-round3.json)
records final source hashes, contracts, SDK/host evidence and native CPU results.
The [before/after audio review](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Release/validation/physical-models-round3-20260922/physics-review.html)
compares the four instruments at common gain using the same 14-second score in
four setups, including exposed quiet restrikes after a loud note. Native renders and numerical contracts do not establish subjective
pleasantness or a completed listening assessment.

Two previous test assumptions are replaced with more direct physical checks.
A multitone off-centre restrike can feed the odd mode from other modes, so its
final odd-mode level cannot isolate absorption; a seeded isolated-mode contact
test does. A freshly enabled sum of detuned resonators can briefly exceed its
steady forced response, so bypass correctness now requires exact equality to a
never-before-enabled reference, instead of ordering their output energies.

## Final measured results

All mandatory contracts passed, including all 11 factory patches at
44.1/48/96 kHz. AddressSanitizer and UndefinedBehaviorSanitizer passed the new
beam, contact and third-round response tests. No factory patch trims or
existing level/velocity/rate thresholds were changed. Both x86_64 and arm64
SDK 5 local45 Deployment libraries were built and the 46 installed files were
verified and archived.

A fresh, protected Recon 14.0.2d7 build 20275 process created the current device
and passed its nonlinear value-conversion checks. Its binary matches the
retained SDK 5 capability audit. Production files and the original scratch
state were preserved. Recon abbreviates `1.3.0b4` to `1.3b4` in its log; the
initial exact-string parser rejected that display format. Reassessment of the
same retained log confirms the expected version and module. No host retry was
needed. CLI creation does not establish interactive playback, named patch
loading, song persistence or host CPU performance.

Native performance on Apple M1 Max, 48 kHz, 64 frames and 16 voices:

| Model | Previous median CPU/block, 2x | Revised median CPU/block, 2x | Change |
| --- | ---: | ---: | ---: |
| Rosewood | 178.9 µs | 187.3 µs | +4.7% |
| Padauk | 160.5 µs | 167.8 µs | +4.5% |
| Balafon | 175.6 µs | 183.3 µs | +4.4% |
| Kalimba | 136.5 µs | 143.6 µs | +5.3% |

Normal default-2x probes had zero wall and thread-CPU deadline misses. With
16-note simultaneous restrikes every 32 blocks, the revised engine had two
wall-clock misses and zero thread-CPU misses over 16,384 measured blocks;
the saved baseline had 17 and zero respectively. The revised burst p99 CPU
maximum was 516 µs against a 1333 µs deadline. These paired clocks separate
elapsed-time overruns from charged CPU but do not guarantee host scheduling.
Optional 4x recorded six wall/zero CPU misses; optional 8x recorded 38 wall/21
CPU misses. All runs are preserved, without retrying for a passing result.

The 32 audio files use one common gain, with a maximum peak of −5.86 dBFS.
For the exposed quiet-restrike score, wood-model differences measure
−13.5 to −14.6 dB relative to the previous output. Standard separated strokes
change much less, especially in Padauk and Balafon, as expected from contact
that responds to an existing ring. The Kalimba hard-contact Far comparison
changes by −35.2 dB relative to its previous signal. These measurements show
where to listen; they are not a subjective preference score.
