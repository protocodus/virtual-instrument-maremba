# Vector kernels and builtin math for 1.6.5

Reason's toolchain compiles `Source/DSP` with `-fno-builtin` and without
auto-vectorization. The loops that 1.5.7 and 1.6.1 laid out "so the compiler
vectorizes them" therefore ran one float at a time in the Rack Extension, the
build that overloaded, and only a native build had the benefit. 1.6.5 writes
those loops with GCC/Clang vector extensions (`Source/DSP/SimdLanes.h`:
two doubles or four floats, SSE2 and NEON), which need no auto-vectorizer,
sums the sympathetic halo once per octave source, and keeps the per-sample
`abs`, `isfinite` and `sqrt` as instructions. Nothing was removed: the same
modes, voices, rates, filters and orders run, and every sample is the same
bit pattern as in 1.6.4.

## Changes

Every change is graded **exact**: byte-identical output (see Sound). The
counts are callgrind instructions for one workload, Concert Grand Rosewood
playing an eight-note chord every 1.5 s for 3 s at 48 kHz and Quality 1x in
64-frame batches, less the same program's run of zero seconds (engine
construction). "Native" is GCC 13.3 `-O3`; "Reason-like" is Clang 18.1 `-O2
-fno-builtin -fno-vectorize -fno-slp-vectorize -ffp-contract=off`, which
approximates the Jukebox toolchain's scalar code. Each percentage is against
the row above.

| Commit | Change | Native | Reason-like |
| --- | --- | ---: | ---: |
| 1.6.4 | | 4460 M | 5839 M |
| `26c4dd8` | Halo: the 61 receivers' pressures are summed per octave source in one pass, not through one call, source lookup and three memory updates per receiver | 4043 M (−9.4%) | 5432 M (−7.0%) |
| `a3313d1`, `a93f570` | Sympathetic receivers advance two to a vector. Close: the transform stages of eight points and more, the fractional delay, the history write, the spectrum sums and the output scale run four floats to a vector (counted together) | 4017 M (−0.6%) | 4579 M (−15.7%) |
| `19e0d09` | Room: a tap's interpolation runs four frames to a vector | 4018 M (+0.0%) | 4400 M (−3.9%) |
| `7352163` | Per-sample `abs`, `isfinite` and `sqrt` through `__builtin_` forms (`DspMath.h`); the Room's flush is one comparison on a magnitude, not two on a value | 4058 M (+1.0%) | 4279 M (−2.7%) |
| `eb0eef4` | Halo: a run's products and zero tests four to a vector; its sums still one term at a time, in order | 4000 M (−1.4%) | 4203 M (−1.8%) |
| `6d4ba27` | Close: the four-point transform stage in vectors, two spans a pass; a source's partition group by mask instead of a division | 3970 M (−0.7%) | 4137 M (−1.6%) |

Two rows need a note.

- `7352163` adds instructions natively and is kept. GCC compiled the old
  flush, `(x < 1e-20f && x > -1e-20f) ? 0 : x`, as two branches on the sign
  of the sample; the new one is branch-free and one instruction longer.
  Callgrind's branch simulation of the same workload counts 31 M fewer
  conditional branches and 7.3 M fewer mispredicted ones (26.1 M to 18.8 M)
  for the 40 M added instructions. In the Reason-like build both forms are
  branch-free and the new one is four instructions shorter.
- The other half of `7352163` cannot be measured on this machine. With
  `-fno-builtin` Reason's library provides `std::abs`, `std::isfinite` and
  `std::sqrt` as real calls (`std::sqrt` a software routine), while the
  compilers here turn them into single instructions with or without that
  flag. The output headroom stage alone made about eight `std::abs` calls
  per channel and frame. The saving exists only in the SDK build and is not
  in any figure in this document.

## Instruction counts, 1.6.4 against 1.6.5

The same method on the eight demo presets, the stress phrase (an eight-note
chord, restruck note by note, a second chord on top; 4 s) and the higher
Quality rungs (2 s).

| Workload | Native, 1.6.4 | Native, 1.6.5 | | Reason-like, 1.6.4 | Reason-like, 1.6.5 | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ambient Cathedral Bloom, chord, 1x | 4.76 G | 4.30 G | −9.8% | 6.22 G | 4.47 G | −28.1% |
| Concert Grand Rosewood, chord, 1x | 4.46 G | 3.97 G | −11.0% | 5.84 G | 4.14 G | −29.1% |
| Hard Acrylic Virtuoso, chord, 1x | 4.30 G | 3.80 G | −11.7% | 5.62 G | 3.96 G | −29.5% |
| Rosewood Roll Chorale, chord, 1x | 4.71 G | 4.24 G | −9.9% | 6.16 G | 4.41 G | −28.4% |
| Guatemalan Charleo, chord, 1x | 4.15 G | 3.63 G | −12.5% | 5.40 G | 3.80 G | −29.7% |
| Birifor Gyil Buzz, chord, 1x | 3.88 G | 3.79 G | −2.2% | 5.16 G | 3.92 G | −24.1% |
| Chopi Timbila Ensemble, chord, 1x | 4.02 G | 3.83 G | −4.8% | 5.29 G | 3.97 G | −25.0% |
| Kalimba Pick Sparkle, chord, 1x | 4.32 G | 4.47 G | +3.5% | 6.46 G | 4.85 G | −25.0% |
| Concert Grand Rosewood, stress phrase, 1x | 6.52 G | 5.89 G | −9.6% | 8.55 G | 6.12 G | −28.4% |
| Kalimba Pick Sparkle, stress phrase, 1x |  |  |  | 9.38 G | 7.11 G | −24.2% |
| Concert Grand Rosewood, chord, 2x |  |  |  | 5.17 G | 3.75 G | −27.6% |
| Concert Grand Rosewood, chord, 4x | 7.25 G | 5.88 G | −19.0% | 7.99 G | 5.92 G | −26.0% |

The empty native cells were not counted for 1.6.4.

The native build gains less because GCC `-O3` already vectorized the Close
and Room loops; what it gains is the halo's per-octave sum, more at 4x, where
the halo runs at the oversampled rate. The two balafon presets gain least.

**Kalimba costs more natively: +3.5% instructions.** Its 17 tines each
radiate from their own source, so every halo run holds one receiver and pays
the run's setup without sharing it: the capture scene's lines inside
`RenderBatch` rose from 348 M to 462 M in that workload, more than the rest
saved. The Reason-like build still falls by a quarter. Sending
single-receiver runs through `AddCompactPressure`, as 1.6.4 did, would take
this back and is not in 1.6.5.

## Sound

- `Tools/CpuEquivalenceRender.cpp` (32 full-engine cases) and
  `Tools/CpuSparseRestrikeEquivalence.cpp` give the same SHA-256 for 1.6.4
  and after every commit above:
  `073fbebb5c9f0638209a180f59e82ddbbfbd39f533d3069c4d17e825937690fa` and
  `9c690a2c0867f058b5d491bdf92733e541f1d0093993959f51f26f4d3f7b2bc5`
  (GCC 13.3, `-std=c++17 -O3`, x86-64).
- So do 42 more streams, each compared with its 1.6.4 render in both the
  native and the Reason-like build: the eight preset demos, a 1x variant of
  the equivalence cases, five presets playing the chord and the stress phrase
  for 6 s, and the stress phrase at 2x and 4x.
- `Maremba.SimdKernel` (unit tier) compares each vector kernel with the
  scalar loop it replaces, bit for bit, on every platform the tests run on:
  the paired receivers, the halo runs (zeros of both signs, vanishing and
  non-finite terms, sleeping sources), the whole Close capture against its
  one-float build at five rates, and the Room tap. The mutations tried
  against it each fail it: a reversed sum order in the halo run and in the
  delay taps, a flipped sign in a butterfly and in the spectrum sum, and
  swapped Room tap operands.
- No change alters a value, so the 1-ulp sensitivity probe had nothing to
  grade and no listening set was made.

The kernels keep each scalar expression and its statement boundaries in
every lane, and nothing is summed across lanes, so a compiler that
contracts `a*b+c` within an expression (Clang on Arm) contracts the same
products in both forms. The halo's sum adds zero terms the per-receiver
calls skipped: a sum that starts at +0 never becomes −0, so a zero term
leaves it as it was.

## Timing

One engine in Reason's 64-frame batches at 48 kHz, each batch timed with
the thread CPU clock and keeping its minimum over three fresh engines; the
1.6.4 and 1.6.5 binaries ran alternately, twice, and both runs are shown.
Intel Xeon @ 2.10 GHz cloud container shared with other builds (load average
above 20 on four cores), 2026-11-10, so these are indicative only; the
instruction counts above are the measurement. A batch lasts 1333 µs.

| Build, workload | Mean batch, 1.6.4 | Mean batch, 1.6.5 | Worst batch, 1.6.4 | Worst batch, 1.6.5 |
| --- | ---: | ---: | ---: | ---: |
| Reason-like, Concert Grand Rosewood, chord, 1x | 158, 156 µs | 110, 112 µs | 999, 856 µs | 952, 928 µs |
| Reason-like, Concert Grand Rosewood, stress, 1x | 175, 162 µs | 113, 116 µs | 977, 889 µs | 931, 869 µs |
| Reason-like, Kalimba Pick Sparkle, stress, 1x | 200, 186 µs | 138, 134 µs | 674, 594 µs | 643, 565 µs |
| Reason-like, Birifor Gyil Buzz, stress, 1x | 147, 147 µs | 106, 105 µs | 922, 1173 µs | 914, 940 µs |
| Reason-like, Concert Grand Rosewood, stress, 4x | 369, 368 µs | 264, 248 µs | 1156, 1391 µs | 1187, 1194 µs |
| Native, Concert Grand Rosewood, stress, 1x | 141, 151 µs | 122, 121 µs | 727, 835 µs | 831, 839 µs |
| Native, Concert Grand Rosewood, stress, 4x | 350, 340 µs | 277, 278 µs | 1127, 1371 µs | 1208, 1048 µs |

The mean batch falls by about 30% in the Reason-like build. **The worst
batch does not move**: it is the batch in which eight keys are struck, and
that batch is note-on work, not these kernels. In the Reason-like profile a
note-on (`MarembaEngine::TriggerNote`) costs about 382,000 instructions,
so eight in one batch add about 3.1 M to a batch that otherwise runs about
1.8 M. That is what remains between a chord and a 64-frame deadline, and
none of 1.6.5's changes touch it.

These are development-machine figures, not Reason or DAW CPU.

## Maremba.IdleFlushCost fails on the development machine

`Maremba.IdleFlushCost` (perf tier) requires the batch that falls idle to
cost no more than twice a ringing batch. On 1.6.5 it fails on this machine,
six runs of six under load and again with the machine nearly idle, while the
CI runner's perf tier passed on the same commit (its figures were not read).
The test has lost its margin; it does not fail everywhere. The same test
built against 1.6.4 and against 1.6.5 (GCC 13.3 `-O3`), run alternately six
times on this machine:

| | Ringing batch, median | Batch that falls idle | Ratio |
| --- | ---: | ---: | ---: |
| 1.6.4 | 71–73 µs (one run 90) | 118–134 µs | 1.4–1.8 |
| 1.6.5 | 53–65 µs | 118–191 µs | 2.0–3.6 |

The idle batch did not get slower: its best run is 118 µs in both. The
ringing batch of the test's single held key got about a quarter cheaper, so
the same flush is now more than twice it. 1.6.3 met the same thing when the
halo became cheaper and answered by making the flush cheaper. That has not
been done here; the limit was not changed. The idle batch is 8% of a
64-frame batch at 44.1 kHz (1451 µs).

## Where the Reason-like build spends the rest

From a profile of the chord workload after these changes.

- **Note-on** (see Timing): the strike batch is the worst batch, and a
  chord's note-ons all land in one. Making `TriggerNote` cheaper, or
  spreading a chord's voice setup, is what would lower the peak.

- **Voices** (`MarembaVoice::ProcessSample`, about 18% at 1x, more than a quarter at 4x):
  double-precision modal and contact physics with data-dependent branches.
  No loop in it holds more than a few percent.
- **The Room's feedback networks** (`FeedbackNetwork::Absorb` and
  `Process`, about 11%): twelve delay lines a tick, each through its own
  fractional allpass, loop diffuser and shelves. The lines are independent
  within a tick, so they can run four to a vector with identical results,
  but their state lives in per-line objects with per-line modes; it needs
  a structure-of-arrays layout first. This is the next target.
- **Sympathetic receivers** (about 10% at 1x, more at 2x and 4x, where they
  run at the oversampled rate): 122 double-precision resonators a substep,
  now about 18 instructions each. The halo's sum is another 5%.
- **Close bookkeeping** (`CloseCapture::Process`, about 5%): flight writes
  and source lists per active source and frame, no kernel.
- **Early-reflection scatter** (about 3%): a serial allpass chain, each stage
  feeding the next, so it has no lanes.
- The Close transforms' first forward stage and two-point inverse stage are
  still scalar (about 2% together): their butterflies pair neighbouring
  floats, which takes single-float lane shuffles. Left for a later pass.
- The Windows plug-in is built with MSVC, which has no vector extensions,
  and runs the scalar loops (`MAREMBA_SIMD` is 0 there).
