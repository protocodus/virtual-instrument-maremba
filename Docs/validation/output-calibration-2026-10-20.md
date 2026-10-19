# Main output calibration — 2026-10-20

## Reference and scope

All four initialized models and all 20 factory presets match **−27 dBFS
stereo RMS** on a shared five-second, velocity-80 phrase with **Linear**
velocity response. RMS is `sqrt(sum(L²+R²)/(2 × frames))`, including the
phrase's tail. The notes are MIDI 60, 64, 67, 72, 67, 64, 60, 60, 62, 65,
69, 74, 69, 65, 62, 62, 48, 55, 36, 84; onsets are 150 ms apart and holds
are 100 ms. Sample rate is 48 kHz, default 2× quality, with the product's
player, parameter mapping, physical controls and capture paths.

**−7 dBFS is the Main peak ceiling, not a requirement to raise every sound's
peak to −7.** Initialized model peaks on this reference are about −11.9 to
−13.1 dBFS. Their RMS is equal to that of the sharper factory sounds.
Insisting on identical peaks as well as identical RMS would require much
stronger compression; a trial at −21 dBFS RMS needed 25.2 dB peak reduction
and 14.1 dB energy loss on Equipentatonic Log Keys. The chosen calibration
preserves more of the attack and allows naturally lower peaks.

The existing 2 ms look-ahead gain controller now uses a separate fixed
Main ceiling of `10^(−7/20)`. It anticipates transients without samplewise
waveshaping. The five direct outputs retain their fixed recording trims,
independent groups and 0.94 ceilings. Below the detector threshold, audio is
unchanged apart from the existing delay. Musical Limit retains its existing
bypass range; this Main peak reservation also works with Limit at zero.

Normal reference reduction is zero for all four initialized models. Among
factory presets, all except **Equipentatonic Log Keys** need at most 2.401 dB
peak reduction and 0.274 dB energy loss. Equipentatonic needs **7.525 dB peak
reduction and 2.401 dB energy loss even at normal velocity 80**. This exception
is intentional and should not be described as overload-only processing.
The [solver measurements](main-calibration-solutions-2026-10-20.csv) retain
per-state peak, RMS and reduction. These are sample-peak measurements, not a
certified true-peak mastering specification.

## Fixed calibration and volume

`MainCalibration.h` holds model gains 1.225115902, 1.174502045, 1.406057112,
1.034682015. Every preset stores a fixed Volume value; no runtime RMS
measurement or automatic level matching is added. The preset bank is generated
from the Rack adapter's patches, with only Volume and velocity-curve choice
changed among its sound controls. Bare init and factory patches now select
Linear; saved patches retain their stored curve choices.

Volume retains its 85% unity point, square law below it, 5 ms smoothing and
continuous slope at the join. Its upper region now reaches 2.25× (+7.04 dB),
down from 4× (+12.04 dB). Full Volume remains subject to the Main ceiling;
it can increase average level and limiting. Existing saved Volume positions
are retained and intentionally sound different under the new gain/ceiling.
Physical modeling, Drive, musical Limit controls and direct stems are unchanged.

## Reproduction

Build the DSP-only CMake targets, including `MarembaMainCalibrationTests` and
`CalibrateMarembaMain`. Capture an unclipped source with:

```sh
MarembaMainCalibrationTests --quiet --capture /fresh/capture-directory
CalibrateMarembaMain /fresh/capture-directory -27
```

The capture command refuses an existing directory and writes 24 native float32
stereo files at 48 kHz, without normalization. Volume .2125 has gain .0625.
For a solver result `g`, the new initialized model gain is
`old_model_gain × .0625 × g`. A preset's required Volume gain is
`.0625 × g × old_model_gain / new_model_gain`; invert `MainVolumeGain` to
store its knob position. The [calibration record](main-calibration-2026-10-20.json)
retains the measured old gains and final preset values.

The offline solver replays the quiet capture through the production protector
for each trial amplitude, solving final protected RMS rather than raw RMS.
Its extra 2 ms tail truncation changes these fixtures by at most 0.0000053 dB;
the final contract independently renders the actual engine path.

## Validation scope

The normal and stress contract **passed all 216 cases** across 24 states at
44.1, 48 and 96 kHz.
Stress means Linear velocity 127, Volume 1, the phrase transposed down 24
semitones (MIDI 12–60), and an eight-note low chord (MIDI 24, 28, 31, 36,
40, 43, 48, 52). Every Main sample must stay below the −7 dBFS ceiling, and
each direct sample below 0.940002. Reference RMS tolerance is ±0.35 dB. Measured RMS spans -27.029 to -26.906 dBFS.

Per-case results: [44.1 kHz](main-levels-44100-2026-10-20.csv),
[48 kHz](main-levels-48000-2026-10-20.csv),
[96 kHz](main-levels-96000-2026-10-20.csv).

The independent headroom contract passed 49,944,359 checks across eight rates
from 22.05 to 192 kHz. It verifies quiet delay, linked stereo gain, byte-identical
direct stems and the lower Main FIR detector threshold. Restoring the former
FIR activation threshold in a scratch copy makes the new witness fail.
Controls and explicit Sigmoid/Linear response contracts passed; the Sigmoid
matrix retains 2,412,825 checks.

The unit-tier run initially passed 81 of 83 registrations. Two tests still
encoded the previous recording gains or implicitly selected the former default
curve. Their routing oracles and historical fixture curve were corrected
without relaxing physical bounds; both full contracts (`Engine` and
`MainPickupAlignment`) then passed on rerun. One duplicate calibration
registration was removed, leaving **82 distinct unit tests with passing results
on the final source**. The hardest-hit matrix, reconstructed-output probe,
parameter reachability, room measurements and calibration tools also passed.
The final unit tier was not repeated wholesale after those test-only fixes.



The native default-quality [CPU probe](default-cpu-probe-2026-10-20.cpp) measures
48 kHz, 64-frame blocks, 2× quality, a 16-voice limit, Main only, four-note
velocity-80 chords every 0.5 seconds, all four initialized models, 256 warm-up
and 4,096 measured blocks per model. Note events are included in each timed
block. The workload reaches 13 active voices. [Timing results](default-cpu-2026-10-20.csv)
retain paired wall/thread-CPU distributions and deadline counts. Across all
16,384 measured blocks, wall and CPU deadline misses are both zero against the
1,333.33 µs block deadline. This is native engine timing, excluding the Rack
wrapper, Reason scheduling and target translation; it is not host CPU evidence.
Compile the retained probe as C++17 with `-O3`, `-I Source/DSP` and the built
`libMarembaDSP.a` to repeat it on this POSIX host.

This is source/native validation. No new plug-in installation, SDK delivery
archive, host playback session, hosted CI or publication is claimed.
