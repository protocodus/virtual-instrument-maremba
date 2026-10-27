# CPU: capture-scene trims (1.5.7)

Six optimisations, one mechanism per commit, take **13.8 % of the
instructions** out of a Rack-like build rendering every factory preset. Each
one is graded *exact*: the float32 output of the regression stream is
byte-identical to the 1.5.6 baseline on every preset, at every step, as are the
two existing optimisation oracles. Nothing about quality, voices, rates,
latency, noise or controls changed, and no test or fixture was touched.

## Protocol

The harness is [`Tools/CpuPresetRegression.cpp`](../../Tools/CpuPresetRegression.cpp);
its head states the protocol and the exact commands.

- **Workload.** All 20 factory presets at their stored controls (1x quality,
  16 voices), 48 kHz, 64-frame calls, 1.6 s each: an eight-note melody at
  varied velocity, a held four-note chord with a pitch-bend move, a
  sixteen-note cluster (notes 42–57) struck together, released, and its decay.
  Then the heaviest supported quality, 8x, on the first preset of each model
  (Ambient Cathedral Bloom, Chiapas Folk Fiesta, Ancestral Balafon, Kalimba
  Muted Thumbs) for the first 0.6 s of the same phrase. 34.4 s of audio in all.
- **Builds.** The primary workload is the Rack-like build: C++17, `-O3
  -fno-builtin`, Maremba has no embedded define (Ubuntu clang 18.1.3, x86-64).
  The JukeboxSDK was not available, so this approximates the Rack's
  toolchain; note that host clang still auto-vectorises where the SDK IR
  does not. The normal host build is the same without `-fno-builtin`.
- **Metric.** `valgrind --tool=callgrind` (3.22.0) instruction count (Ir) of
  the rendered stream only (`--toggle-collect='*RenderStream*'`), Main outputs
  connected. MIDI handling is counted; the engine's construction, which the
  Rack does off the audio thread, is not. The machine was shared with five
  other build and test jobs, so no wall-clock or thread-CPU timing is
  reported; instruction counts do not depend on that load.
- **Parity.** Each step was rebuilt with both flag sets and its regression
  stream compared byte for byte with the baseline's: all seven outputs
  connected (46.2 MB of float32) and Main only (13.2 MB), plus the outputs of
  `Tools/OptimizationRender.cpp` and `Tools/CpuEquivalenceRender.cpp`
  (44.1/48/96 kHz, every quality, every room, 48-slot voice steals, live
  control, model and quality changes). The Rack-like and host builds also
  agree bit for bit with each other.

## Results

Rack-like build, callgrind Ir over the stream, baseline `f8589d6` (1.5.6):

| Commit | Mechanism | Grade | Ir | Step | Cumulative |
| --- | --- | --- | ---: | ---: | ---: |
| `f8589d6` | baseline (1.5.6) | — | 173,202,357,180 | — | — |
| `81d71e0` | contiguous fractional-delay taps when no tap wraps (Close flight, Main pickup alignment) | exact | 169,051,991,326 | −2.40 % | −2.40 % |
| `bdbc3c2` | guard slot before each room-capture ring: one index wrap per path read instead of two | exact | 159,183,078,459 | −5.84 % | −8.09 % |
| `e5ef2b4` | skip the unit-twiddle product (j = 0) in every Close-capture FFT stage | exact | 155,089,994,883 | −2.57 % | −10.46 % |
| `fa484f1` | real-input, half-spectrum source FFT (real first stage, real second-stage products, unused differences skipped) | exact | 153,864,826,107 | −0.79 % | −11.16 % |
| `d2ea35e` | room-capture discovery tests "already queued" before reading the input | exact | 152,548,193,641 | −0.86 % | −11.92 % |
| `9db7b48` | compact-source capture input takes one finiteness and one zero test instead of three each | exact | 149,359,548,448 | −2.09 % | −13.77 % |

Host build (`-O3`, no `-fno-builtin`), same stream: 182,762,420,947 Ir at the
baseline, 159,018,345,080 after `9db7b48`, **−12.99 %**.

Where the instructions were, in the baseline's Rack-like profile: the capture
scene took 63 % of the stream (Close HRTF convolution 33 %, room capture 27 %),
voice synthesis 14 %, the sympathetic mesh 4 %. About fifty capture sources
are live at once in these presets, because every sympathetic receiver is a
compact source of its own, so per-source and per-path work dominates.

### Why each is exact

- **Contiguous taps.** When the read point is at least taps − 1 into the ring,
  the window is read through a pointer; otherwise the wrapping loop runs. Same
  products, same summation order.
- **Ring guard slot.** Each source's ring starts one slot into its stride; that
  slot is written with every write to the ring's last sample, so reading index
  −1 returns exactly what the wrapped index returned. The stride already had
  the spare slot at the maximum rate: no memory is added.
- **Unit twiddle.** `mTwiddle[0]` is (1, −0); the full product differs from the
  input only in the sign of a zero part. With finite values a zero's sign can
  reach only another zero, and every FFT result leaves the renderer through
  `Flush`, which writes any zero as +0 (the filter spectra enter only products
  that do). Histories, convolution and output are therefore bit for bit
  unchanged.
- **Real half spectrum.** Source blocks are real (imaginary +0) and only bins
  0..N/2 are stored. First-stage sums keep +0 imaginary parts exactly; a +0
  imaginary part changes the second stage's products only in the sign of a
  zero; the last stage's differences above N/2 are never read.
- **Discovery order.** The skipped computation is a pure function whose result
  was discarded for an already queued source.
- **Compact input.** `AddCompactPressure` passes (p, p, 0.6·p): 0.6·p is finite
  and zero exactly when p is, and equal ear inputs never separate the ears, so
  the reduced tests take the same branches.

## Tests

After each commit, the unit-tier suites covering the changed code (capture,
room, pickup alignment, routing, scene equality, direct outputs, headroom)
passed. On the final tree: the full `unit` tier, the `fuzz` tier
(`ExtremeRange`, `EngineFuzz`, `ParameterMatrix`) and the `MarembaDSPCxx17`
target (C++17, `-Wall -Wextra -Werror`): all 83 unit tests and all 3 fuzz
tests passed, and the C++17 target built cleanly. The `audio` tier (the
preset demos) was not run.

## Candidates not shipped

- **Mirrored room history.** Writing each ring twice would remove the
  remaining wrap from every room path read (estimated another 3 % on this
  workload), exactly, but doubles the room history: 5.8 MB more at 48 kHz,
  23 MB at 192 kHz.
- **Modal microphone readout cache.** Folding `cosW·invSin` and `r·invSin`
  into per-mode microphone gains (the Rack's 2026 voice investigation) changes
  the arithmetic, so it is last-bit at best and needs the one-ulp control probe
  and invalidation on every retune, bend and damping change.
- **True real FFT, or summing same-profile spectra before the filter
  multiply.** Both would cut the convolution substantially but reassociate
  float sums; at best measured-inaudible, so they wait for a null test.
