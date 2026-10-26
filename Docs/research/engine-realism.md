# Maremba 1.1 engine changes

Historical 1.1.0f1 engine notes. Attack pitch glide was subsequently removed in
1.2.1f1; its saved property is now inactive. See the [current release evidence](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.2.2f1.md).

Historical 1.1 evidence. The subsequent audio-only 1.2 update is described in [touch-engine.md](touch-engine.md) and [touch-response.md](touch-response.md).

This is an intentional sound update. Parameter IDs, ranges, device defaults, the four instrument identities, oversampling choices, and the allocation-free render contract stay intact. Existing songs can sound different. Factory Direct Piezo Mallet receives a separate volume calibration from 0.75 to 0.87 to retain the bank's established loudness window.

## What changed

- **A strike has a physical location.** Signed free/free beam mode shapes replace generic distance-from-centre gains. A centred ideal strike suppresses antisymmetric modes; moving across a node changes a mode's amplitude and phase. Each mode is averaged analytically across a finite contact width, so wider/softer strikers cancel short spatial wavelengths. Existing strike variation perturbs the location naturally. These calculations happen at note-on.
- **Kalimba geometry is distinct.** Clamped/free cantilever shapes replace the symmetric bar-position response. The position control traverses the playable final 35% of the tine. Fundamental normalization keeps the default playable; excitation rises toward the free tip. The existing thumb-contact pulse remains an approximation, not a solved preload-and-release contact model.
- **Tuned bars keep their middle register.** Instrument ratios remain intact through C5. A smooth transition toward uniform-bar ratios runs from C5 to C7, for all eight modes. Previously the transition began at C4 and was complete at C6; only five modes transitioned. The new bounds are voicing choices.
- **Restrikes conserve the existing ring.** Changing decay or tuning at a restrike now transforms both delayed states to the new pole radius/angle before applying contact damping. Previously the same stored states could gain or lose energy immediately; regression cases found up to approximately 29.5% spurious energy growth. Pitch-glide updates now use the same phase-preserving transformation and keep the energy estimator current.

The spatial shapes are **uniform-beam approximations combined with voiced undercut-bar frequency ratios**. Real undercut bars have different node positions, asymmetry and torsional modes. The mode weights, contact widths, register transition and material losses are engineering choices, not measurements of a named instrument. The resonator remains one-way driven; this update does not claim a fully coupled finite-element instrument model.

[Beaton and Scavone's experimental marimba study](https://pub.dega-akustik.de/ISMA2019/data/articles/000040.pdf) supports the conventional 1:4:10 tuning anchor and documents real geometry/modal variability. [Oliveira's timbila research, chapter 6](https://run.unl.pt/server/api/core/bitstreams/685e9a24-6eaa-4026-862a-364774cb1221/content) supports evaluating excitation at spatial mode shapes and distinguishing bar, mallet and support dynamics. Neither source supplies Maremba's voicing constants.

## Validation

The existing engine suite passes, including rate invariance, tuning, all model/quality paths, allocation tracking, voice lifecycle, live retuning, denormals, controls, and parameter fuzzing. New rendered-audio contracts verify centre-mode suppression, nodal suppression, increasing tine response toward its tip, and middle-register tuning. New state-energy contracts cover altered decay/tuning on restrike for four models, three registers, and two internal sample rates. The new checks fail 32 assertions against the previous engine; [that negative-control run](../validation/engine-old-audio-regressions.txt) is retained alongside [the passing run](../validation/engine-tests-1.1.txt).

## Native performance

2026-09-19, Apple M1 Max arm64, Apple clang 21.0.0, `-O3 -std=c++17`. The probe renders 4,096 measured 64-frame blocks after 256 warm-up blocks, 48 kHz, 16 ringing voices with a repeating strike every 32 blocks. It uses shipped defaults except the selected oversampling factor. Timing includes parameter delivery, note-on and all seven outputs. Block deadline: 1,333.33 microseconds. Baseline source: `2b771f4b655ca54471e6ff9fe98fce88c23ccf25`.

| Quality | Baseline median thread CPU | Final median thread CPU | Final wall / CPU deadline misses |
|---|---:|---:|---:|
| 2x (default) | 70.208 µs | 72.166 µs | 0 / 0 |
| 4x | 139.208 µs | 140.209 µs | 0 / 0 |
| 8x | 274.333 µs | 278.250 µs | 0 / 0 |

The final default median is 2.8% above baseline and consumes 5.4% of the native block deadline. The first updated measurement preceded the SDK-compatible replacement of `sinh(h)` with its equivalent exponential/Taylor expression at note-on. Raw [baseline](../benchmarks/engine-cpu-baseline.txt), [first updated](../benchmarks/engine-cpu-1.1.txt), and [final-source](../benchmarks/engine-cpu-1.1-final.txt) results retain all runs, including the baseline's two 2x wall-clock misses and the first update's 8x wall-clock miss. Paired thread timing stayed below the deadline in every measured block; the final run does not erase earlier wall-clock misses or establish Reason/target CPU performance. [Source hashes](../validation/engine-source-sha256.txt) identify the final tested implementation and probe.

Reproduce the current native probe from the device directory:

```sh
clang++ -std=c++17 -O3 -Wall -Wextra -Werror Tests/test_dsp.cpp DSP/MarembaEngine.cpp DSP/MarembaVoice.cpp -o /tmp/maremba-dsp
/tmp/maremba-dsp
/tmp/maremba-dsp --benchmark
```

For a baseline comparison, compile the same probe against the baseline revision's DSP sources and headers, retaining the new `ModalExcitation.h` only for compiling the unused new tests. The benchmark exercises the old engine implementation. Native timing excludes the Rack wrapper, Reason scheduling, and SDK target translation.
