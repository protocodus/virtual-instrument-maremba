# CPU optimization for 1.6.1

Five optimizations, each its own commit, each chosen from a callgrind profile
of the `02-concert-grand-rosewood` preset demo through `MarembaPlayer`. In
1.6.0 that profile spent about 39% of its instructions in the Close capture
(the per-source fractional delays and the partitioned HRTF convolution), 20%
in the Room capture and 13% in the capture scene's emitter bookkeeping.

| Commit | Change | Instructions, demo 02 |
| --- | --- | ---: |
| `ff32e76` | Close: each ear's 8-tap fractional delay runs once per partition, vectorized across frames, instead of once per frame | −6.2% |
| `ecb14d5` | Room: block tap sums stored per channel and wall, contiguous across frames, so the tap loop vectorizes | −5.7% |
| `6debe39` | Capture scene: emitter sums checked for overflow once per frame instead of after every addition | −1.8% |
| `aa7d7e3` | Close and Main pickup delay lines: wrap-free common paths | −1.0% |
| `ace3dd2` | Close: FFT stages read local twiddle copies, removing a per-span runtime aliasing check (forward FFT −39%) | −5.3% |

Callgrind totals for that demo: 86.49 G instructions in 1.6.0, 70.42 G in
1.6.1 (−18.6%).

## Sound

Every change keeps each sum's operands and order, so the output is
bit-identical, not merely inaudibly different:

- `Tools/CpuEquivalenceRender.cpp` (32 full-engine cases: four models,
  44.1/48/96 kHz, quality and room paths, seven outputs, irregular calls,
  steals, live controls) and `Tools/CpuSparseRestrikeEquivalence.cpp` give
  the same SHA-256 against 1.6.0 after every step:
  `caff04b0f81eea870e22275b6dcbb1119cd5db04639b8e66a60a50cafed88350` and
  `79a7e21ee45cfb6ebd2ed66a168003e86c2b12e9391dab96ef064ef027ad19c9`
  (GCC 13, `-std=c++17 -O3`, x86-64).
- All eight README preset demo WAVs rendered by `RenderMarembaPresetDemos`
  are byte-identical to 1.6.0 after every step.

## Timing

`RenderMarembaPresetDemos --cpu --repeats 5`, Release (GCC 13, `-O3`),
one thread, on an Intel Xeon @ 2.10 GHz cloud container,
2026-11-05. The 1.6.0 and 1.6.1 binaries ran alternately twice; each figure
is the mean of the two runs' medians, as a fraction of real time (lower is
better). The container is shared and noisy: single runs of one demo varied by
up to 15%, so read the per-demo figures as approximate and the instruction
counts above as the stable measure.

| Demo | 1.6.0 (x realtime) | 1.6.1 (x realtime) | Change |
| --- | ---: | ---: | ---: |
| 01-ambient-cathedral-bloom | 0.3037 | 0.2480 | -18.4% |
| 02-concert-grand-rosewood | 0.2839 | 0.2410 | -15.1% |
| 04-hard-acrylic-virtuoso | 0.2597 | 0.2361 | -9.1% |
| 06-rosewood-roll-chorale | 0.2725 | 0.2610 | -4.2% |
| 09-guatemalan-charleo | 0.2816 | 0.2344 | -16.8% |
| 12-birifor-gyil-buzz | 0.1477 | 0.1355 | -8.3% |
| 13-chopi-timbila-ensemble | 0.1892 | 0.1605 | -15.1% |
| 19-kalimba-pick-sparkle | 0.1296 | 0.1133 | -12.6% |
| All eight (sum) | 1.8679 | 1.6298 | -12.8% |

These are development-machine figures, not Reason or DAW CPU.

## Tried and dropped

Transforming eight source blocks at once, one per SIMD lane, to vectorize the
FFT's short early stages was bit-identical but slower, and fusing the first two
FFT stages saved only 2% of the transform; neither was kept.
