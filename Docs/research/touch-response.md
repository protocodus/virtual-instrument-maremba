# Touch response: independent audio measurements

The archived measurements and hashes below describe the 1.2.0f1 contact-engine
release. Later releases rerun the same acceptance contracts; see
[release evidence](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.2.2f1.md) for the current build.

`Tests/TouchResponseTest.cpp` exercises the public `MarembaVoice` API and measures
its rendered close, far and piezo samples. It does not read modal state, include
the contact-shape implementation, or compare against hard-coded voicing weights.
The test is independent of the existing DSP verification suite.

## Method

The complete C4 matrix covers all four models, all four mallet/thumb types,
velocities **1, 8, 16, 32, 64, 96, 127**, and positions **5%, 10%, 22.42%, 35%,
50%, 65%, 95%**: 784 strikes. The additional register/rate matrix covers C3, C5
and C7; strikers 0, 1 and 3; velocities 16 and 127; and internal sample rates
88.2, 96 and 192 kHz, giving 1,000 recorded rows in total. Additional focused
strikes measure transient noise balance, modal symmetry and restrike damping.

Every strike renders 180 ms. Hardness is 0.4, resonator coupling 0.7, decay 1,
and random artifacts, strike jitter, buzz control and pitch glide are zero.
The PRNG seed is identical between controlled comparisons. There is no mixer,
room, preamp, compressor, loudness normalization or delivery codec in this
probe. The separate [demo report](../benchmarks/demo-renders.md) covers exported
main-bus audio. Neither probe establishes an acoustic match to a real recording.

Measurements distinguish the attack (0–8 ms) from the ringing body (12–160 ms):

- **Level:** mean-square energy in each window, in dB relative to unit amplitude.
- **Spectral balance:** a two-pole Butterworth high-pass at three times the note
  frequency, expressed relative to total close-output energy in the same window.
  This is an aggregate spectral measure; it is not a particular partial or a
  perceptual brightness score.
- **Upper-band transient/ring balance:** attack energy above the greater of
  3 kHz and twelve times the note frequency, normalized to body energy. The
  filter is capped at 20% of internal sample rate. This includes both contact
  noise and any pitched partials in that band; it is not a noise-only separation.
- **Temporal touch response:** the least-squares constant-gain fit between
  velocity-16 and velocity-127 close-output waveforms over the first 8 ms.
  Residual energy detects a change beyond gain, including contact-induced phase
  movement. This is not a brightness score or a direct contact-duration estimate.
- **Microphone phase:** correlation and coherent-sum energy of the raw close and
  far outputs. The reference is the maximum coherent sum at their measured
  levels, so destructive cancellation appears as a negative mixing loss.
- **Modes:** Hann-windowed Goertzel power in the piezo output after the initial
  transient, tested at the fundamental and the tuned second bar mode. The
  relative null gates allow leakage and finite contact width.
- **Restrikes:** a forte C3 at 35% rings for 45 ms, followed by a velocity-8
  contact at a fundamental node, the center, or 35%. Decay is 4 for this focused
  test. The following ring reveals which existing modes the new contact damps.

## Acceptance criteria

The gates protect broad intended behavior, rather than reproducing the DSP
formula. At a fixed touch, ringing level must increase with velocity; the full
velocity span must exceed 20 dB. Mixing close and far must not lose more than
6 dB to destructive phase cancellation. Ring level may vary by at most 1 dB
across the tested internal rates. Controlled center/node bar strikes must
suppress their respective modes by at least 15 dB, while a tine's free-tip
fundamental must exceed its inner-contact fundamental by more than 1 dB.

For the contact update, soft-yarn C5 attacks must show more than 0.15 dB of
upper-band fraction growth between velocities 16 and 127. C7 has a broad 1 dB
non-darkening tolerance: soft high bars can remain naturally dark, and a moving
contact-pulse spectral zero can reduce an individual partial. At both C5 and C7,
a constant gain fit must leave more than 1% residual attack energy. Together
these checks reject a velocity-dependent gain applied to an otherwise fixed
treble pulse, without forcing each treble partial to become louder.
At controlled antinodes in C3/C4, the upper-band transient relative to the ring
must not become more than 1 dB weaker from velocity 8 to 127: a quiet touch
should not acquire a disproportionate contact click. A restrike at a node must
protect the existing fundamental by more than 0.25 dB compared with a center
contact; a center restrike must similarly protect the antisymmetric second
mode compared with an off-center contact. Individual partials are not required
to grow monotonically with velocity, because a finite contact pulse can have
spectral zeros. Kalimba spectral changes are measured without assuming that a
linear tine must brighten identically to a struck wooden bar.

## Protected baseline

The baseline source was copied before the contact changes. Its complete hashes
are in [touch-baseline-source-sha256.json](../validation/touch-baseline-source-sha256.json).
The [full matrix](../validation/touch-baseline.csv), [summary](../validation/touch-baseline-summary.txt)
and [negative-control result](../validation/touch-baseline-regressions.txt) are retained.

| Baseline measurement | Result |
| --- | ---: |
| Smallest adjacent-velocity ringing-level increase | +3.069 dB |
| Smallest velocity-1 to velocity-127 dynamic range | 50.895 dB |
| Worst close/far coherent mixing loss | −0.047 dB |
| Largest ring-level difference between rates | 0.102 dB |
| Lowest soft C5 attack spectral growth | −0.027 dB |
| Lowest soft C7 attack spectral growth | −0.009 dB |
| Lowest treble temporal gain-fit residual | 0.0020% |
| Worst upper-band transient/ring growth, velocity 8 to 127 | −10.831 dB |
| Lowest fundamental-node restrike protection | −1.435 dB |
| Lowest center/antisymmetric-mode restrike protection | −0.069 dB |

The baseline passes the original level, mode, rate and phase conditions, and
fails five new contact-response gates. In the C4 sweep, 29 of 112
model/striker/position combinations become more than 1 dB less spectrally bright
in their attack between velocities 1 and 127; these are recorded as observations,
not a gate, because that count includes spatial nodes. The native matrix and
focused measurements complete in approximately 0.4 seconds on the development
machine; compilation and file output are additional costs.

## Final contact implementation

The final source passes all acceptance gates. The complete [final matrix](../validation/touch-final.csv),
[summary](../validation/touch-final-summary.txt), and [source/test hashes](../validation/touch-final-source-sha256.json)
are retained. The source hashes were checked again after capturing the results.
All 1,000 CSV rows contain finite metrics and bounded peaks. The test completed
in 0.375 seconds on the development machine.

| Measurement | Protected baseline | Final implementation |
| --- | ---: | ---: |
| Minimum adjacent-velocity ring increase | +3.069 dB | +3.123 dB |
| Minimum full velocity dynamic range | 50.895 dB | 50.410 dB |
| Worst close/far coherent mixing loss | −0.047 dB | −0.003 dB |
| Maximum ring-level rate difference | 0.102 dB | 0.011 dB |
| Minimum soft C5 attack spectral growth | −0.027 dB | +0.250 dB |
| Minimum soft C7 attack spectral growth | −0.009 dB | +0.048 dB |
| Minimum treble temporal gain-fit residual | 0.0020% | 24.000% |
| Minimum upper-band transient/ring growth, v8 to v127 | −10.831 dB | +0.672 dB |
| Minimum fundamental-node restrike protection | −1.435 dB | +8.657 dB |
| Minimum center/antisymmetric-mode restrike protection | −0.069 dB | +7.314 dB |
| C4 attack spectral reversals greater than 1 dB | 29 / 112 | 0 / 112 |

The main improvements are the removal of disproportionately clicky quiet
attacks, a real temporal response to touch in soft treble notes, and damping
that depends on where the new contact touches an existing mode. Dynamic range,
rate consistency and microphone phase behavior remain healthy. The C7 result
does not imply a bright high-register yarn tone: its broad spectral fraction
changes only slightly, while the contact-driven attack waveform changes
substantially. A contact-pulse spectral zero can move through an individual
partial, so the test deliberately avoids a per-partial monotonicity rule.

A supplementary [dense diagnostic](../validation/touch-dense-diagnostic.txt) covers
every adjacent MIDI velocity from 1 through 127: all 14,112 close/far ringing and
close attack comparisons increased. Five piezo-only decreases occurred at a
nearly silent fundamental node (approximately −101 to −104 dB), consistent with
modal/contact cancellation. A 21-position sweep and 336 tiny position-perturbation
pairs found no abrupt discontinuity. The diagnostic records its temporary source
hashes, reproduction commands and limitations; finalized test/CSV hashes remain
unchanged.

## Reproduction

From the Maremba device directory:

```sh
clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP Tests/TouchResponseTest.cpp DSP/MarembaVoice.cpp -o /tmp/maremba-touch
/tmp/maremba-touch > /tmp/touch-current.csv 2> /tmp/touch-current-summary.txt
```

`--measure` emits the same measurements while skipping the behavioral acceptance
gates, allowing a failing baseline to be characterized. Finite/bounded-audio
checks remain active in that mode. No generated CSV data is needed to run the
contracts.
