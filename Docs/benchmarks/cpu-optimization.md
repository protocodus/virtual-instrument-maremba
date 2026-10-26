# CPU optimization for 1.3.0f3

> 1.4.0b1 replaces the sympathetic halo described here (receivers on each
> key's own pitch and loss, driven through the cords or the bridge), so the
> contiguous-run cache and `ContiguousCouplingTest` no longer apply; see
> [combined models](../research/combined-models-1.4.0b1.md) and the sixth pass's CPU
> section. The text below describes 1.3.0f3–f5.

Five independent agents examined voice DSP, engine scheduling, shared processing,
the Rack wrapper, and audio/realism preservation. The retained change reduces
native rendering CPU time while producing exactly the same audio as 1.3.0f2.
The actual SDK-built modules subsequently measured 2.1–3.1% median reductions
per instrument across factory-preset musical workloads, with 44 bit-identical
audio comparisons. One mean-timing case remains inconclusive; see the
[SDK-native validation](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/benchmarks/sdk-native-cpu-validation.md). The separate dense
engine-only workload below measured 8.2–8.5%. Live Reason scheduling and portal
target-chip CPU remain separate, unmeasured evidence levels.

## Retained implementation

The sympathetic bank already connects each played note to an ascending list of
nearby receivers, excluding that note itself. These connections form at most
two contiguous runs. `SympatheticMesh` now caches their starts, lengths and
unchanged float weights at construction, allowing the compiler to update
independent adjacent receivers together. Every receiver still receives the same
multiply and addition in the same cross-voice order. The receiver filters and
ordered stereo reduction are unchanged.

All 61 receivers, up to 24 transmission paths per voice, modal precision,
membrane solver iterations, oversampling options, tail thresholds, default
16 voices and every sound control remain unchanged. The cache adds 12,800 bytes
(12.5 KiB) per device and allocates nothing in the render thread. It needs no
invalidation on reset, model or tuning changes because its geometry is fixed.

Other proposed changes were measured separately. Active-slot snapshots,
additional coefficient caches, splitting the receiver reduction, and cavity
specializations either gave no reliable improvement or made the combined
workload slower. They are excluded. Their diagnostic results are retained under
`Release/validation/cpu-optimization/`.

## Repeated native timing

The frozen baseline is commit `cbae012` (1.3.0f2). Measurements used an Apple
M1 Max on 2026-09-23. Both versions were compiled with Apple clang 21.0.0,
C++17, `-O3 -Wall -Wextra -Werror`, without fast-math.
The workload uses 48 kHz, 64-frame calls, 16 voices, 2x oversampling, Wood room,
EngineParameters defaults and all seven outputs. Notes 42–57 at velocity 0.8
restrike together every 32 blocks. Each instrument receives 256 warm-up blocks
and 4096 measured blocks. These are explicit engine settings, not a claim to
benchmark every factory patch.

Three predeclared alternating baseline/optimized pairs were run after other
native compilation and test work stopped. Each block measured elapsed time and
thread CPU time separately. The table reports the median of the three per-run
thread-CPU medians, including parameter updates and note events.

| Instrument | Baseline µs/block | Optimized µs/block | Reduction |
| --- | ---: | ---: | ---: |
| Rosewood | 224.709 | 205.667 | 8.47% |
| Padauk | 226.125 | 206.875 | 8.51% |
| Balafon | 227.583 | 208.459 | 8.40% |
| Kalimba | 202.750 | 186.125 | 8.20% |

There were zero thread-CPU deadline misses over 49,152 optimized blocks, and one
elapsed-time overrun against the 1333.33 µs block deadline. That overrun is
retained. The separate quality-path matrix covers all four models at 2x/4x/8x
with single-note restrikes: median reductions range from 8.14% to 12.05%.
Optimized runs have zero thread-CPU and five elapsed-time misses; baseline runs
have eight thread-CPU and nine elapsed-time misses. The three room algorithms
show 8.27–8.61% median reductions and no deadline misses in either version.
These native results do not establish Reason scheduling or final target CPU.

## Sound and boundary verification

- The 32-case full-engine differential fixture is bit-identical across all four
  models, 44.1/48/96 kHz, all quality/room paths, seven outputs, irregular
  1–64-frame calls, 48-slot steal fades, live controls, quality changes with
  queued/cancelled notes, reset, damping and cable changes. Both streams have
  SHA-256 `c5e7037ca8bf3f289ca355be2717e028406d5b4559ba5fbf79de8c7b2b9044cd`.
- A further 24 direct-voice and four sparse-notification engine cases preserve
  rapid coupling/decay restrikes, retuning, thresholds and tails exactly. Both
  streams have SHA-256
  `8b221c57f53e6841bd6f90e713af40f76416bd9552ebfa6a48cbe84db9dffffc`.
- `ContiguousCouplingTest.cpp` independently pins all 128 physical connection
  lists and compares the new path to the previous indexed scalar accumulation.
  It exercises 1,177,648 exact additions and 65,536 complete receiver-state and
  stereo comparisons, including invalid notes, signed zero, tiny drives,
  retuning, reset and tails. It is registered in the complete contract suite.
- All four regenerated musical showcases have byte-identical WAV and AAC
  audio to f2. Only their version/source provenance and player footer change.

`Maremba_CPU_Validation.json` and `Maremba_CPU_Measurements.log` in the release
candidate contain full timing distributions, hashes, scope and limitations.
`RELEASE_EVIDENCE.md` records SDK, sanitizer, package and host status separately.

To reproduce differential renders, compile each of
`Tests/CpuEquivalenceRender.cpp` and `Tests/CpuSparseRestrikeEquivalence.cpp`
twice: once with the frozen baseline DSP include directory and its two `.cpp`
files, then with the current DSP. Use the flags above, pass an output filename
to each binary, and compare the resulting files byte for byte. Run
`Demos/performance_review.cpp` against both DSP trees with
`-DMAREMBA_HAS_ROOM_TYPES`; `--chord-burst`, `--all-models`, and no argument
select the three measured workloads. Serialize timed runs.
