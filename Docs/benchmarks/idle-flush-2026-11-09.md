# Idle flush cost for 1.6.2

When no voice is left and every output has stayed below the silence level for
the hold time, the engine flushes its tails and goes idle. Up to 1.6.1 that
flush cleared the buffers of all 515 capture sources inside one batch, used
or not: Close delay lines (2.9 MB), spectrum histories (7.4 MB) and partition
blocks (2.1 MB), and Room source histories (5.4 MB). A callgrind run of that
one batch counted 16.1 M instructions, 92% of them in these clears.

1.6.2 clears only what can hold signal. `CloseCapture::Reset` retires every
source, and a source's buffers are cleared when it next starts, as they
already were (`ClearSource`). `RoomCapture::Reset` clears the rows of the
sources still in its active list: a source leaves that list only after
writing zeros over its whole history span.

## Timing

One key struck and held 0.5 s, 30 s rendered in Reason's 64-frame batches at
44.1 kHz through the Rack adapter over its host mock. Each batch is timed with
the thread CPU clock and keeps its minimum over three fresh devices.
Intel Xeon @ 2.30 GHz cloud container, 2026-11-09. "Reason-like" is
Clang 18 `-O2 -fno-builtin -fno-vectorize -fno-slp-vectorize
-ffp-contract=off`, which approximates the Jukebox toolchain's scalar code;
"native" is `-O3`.

| Build, patch | Batch that falls idle, 1.6.1 | Same batch, 1.6.2 |
| --- | ---: | ---: |
| Reason-like, Concert Grand Rosewood | 2536 µs | 325 µs |
| Reason-like, Kalimba Thumb Lullaby | 2627 µs | 326 µs |
| Reason-like, Mayan Dance Padauk | 2474 µs | 362 µs |
| Native, Concert Grand Rosewood | 1991 µs | 242 µs |

The 1.6.2 figures include that batch's own rendering (100–200 µs). A
64-frame batch at 44.1 kHz lasts 1451 µs. In the Reason-like build, all 20
factory patches took 2.4–3.1 ms in that batch on 1.6.1, and so did Shop 1.5.7,
1.5.0 and 1.4.0. A ringing note's batches and the later strikes are unchanged.
The first strike after the engine is constructed measured about 0.1 ms slower
with the same instruction count; construction itself is about 6 ms faster.

`Maremba.IdleFlushCost` (perf tier) now requires the idle batch to cost no
more than twice a ringing batch. Against 1.6.1 it measured 2916 µs against
218 µs and failed; on 1.6.2 it measures 245 µs against 218 µs.

These are development-machine figures, not Reason or DAW CPU.

## Sound

- `Tools/CpuEquivalenceRender.cpp` (32 full-engine cases, including an
  explicit Reset followed by new notes) gives the same SHA-256 for 1.6.1 and
  1.6.2: `bf07476bca35b3592103989445671500705e6dae3ecc72809603129bfbcaee5f`
  (GCC 13, `-std=c++17 -O3`, x86-64).
- Through the Rack adapter, a hash of every output sample matched for the
  single-note scripts above, a four-strike script, and a strike repeated
  after the engine had gone idle.
