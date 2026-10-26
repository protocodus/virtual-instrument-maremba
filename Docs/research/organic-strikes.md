# Organic strike variation

Maremba 1.2.2 keeps a small amount of variation in every contact, even when
Variance and Artifacts are both zero. Repeated notes at the same MIDI velocity
therefore receive changing excitation, instead of an identical pitched impulse
with only a different noise burst. This intentionally changes existing patches'
rendered sound while retaining their property IDs, values and automation paths.

## What changes between hits

A new bounded character is drawn at each note-on for the contact or thumb-release
duration, pulse asymmetry, impact strength and individual modal excitation
weights. At zero Variance the maximum deviations are:

| Component | Bound around nominal value |
| --- | ---: |
| Contact/release duration | ±0.75% |
| Pulse asymmetry | ±0.006 |
| Common impact strength | ±0.35% |
| Individual modal excitation weights | ±0.4% |

Each draw averages three independent uniform values, concentrating most hits
closer to the nominal value. These are restrained musical voicing choices, not
measured distributions from a specific player, mallet or instrument. Raising
Variance continuously increases the movement; it also broadens the existing
hard-core shock variation. Artifacts remains the separate control for optional
position, hardness, micro-tuning, friction and mechanical-rattle variation.

The baseline character does not move strike position or retune any mode. Modal
nodes, tuned pitches and the removal of attack glide are preserved. Impact
variation feeds both the tonal excitation and its accompanying impact noise,
so quiet notes do not acquire a separate constant noise floor. Pulse duration
remains fractional and is integrated over each sample; small variations are
not reduced to choosing an integer number of contact samples.

## State and reproducibility

A fixed bundle of 36 PRNG draws is consumed at note-on, independent of Variance
and sample rate. This work introduces no heap allocation or new audio-rate
random calculations. The engine's strike state advances across repeated notes,
voice reuse, silence and normal transport damping. It is separate from the
per-voice audio-noise streams, so changing render block size does not change
which excitation a strike receives.

An explicit audio reset restarts the sequence. Replaying the same event stream
from the same reset state deliberately reproduces the same result, which keeps
offline exports and regression tests repeatable. Successive hits within that
stream vary. Finite pseudorandom state is not a mathematical promise of infinite
uniqueness; regression tests exercise repeated hits and short-pattern rejection
across the supported instrument models and selected conditions.

## Verification

The independent repeated-strike contracts measure the pitched output with
identical noise seeds and without residual tails. They remove best-fit gain
before comparing attacks and sustained waveforms, and separately compare modal
spectral ratios. An additional restrike probe starts each comparison from the
same ringing state so differences cannot be attributed only to accumulated
previous notes. The release evidence records measured ranges, negative-control
results against 1.2.1, tuning/level bounds and deterministic replay.
The [repeated-strike report](../validation/organic-strikes-report.json) includes
the source hashes, all measured cases and acceptance bounds; the
[CPU report](../benchmarks/organic-strikes-cpu.json) records the native workload.

Four fixed-velocity C4 studies in the demo player's Touch category illustrate
repeated strikes with Variance and Artifacts at zero. Their gain is fixed and
they use no parameter automation. Numerical variation checks do not establish
that every pair is perceptually distinguishable or that the model is acoustically
indistinguishable from a recorded instrument.
