# Normal playing level calibration — 2026-10-20

Source candidate: **Maremba 1.5.5**, Rack adapter **1.5.5d1**.
All four initialized models and 19 factory presets now place the common
normal-playing phrase around **−7 dBFS sample peak**. Rosewood Dampened Etude
sits slightly lower, around **−7.8 dBFS**, to preserve gentle protection on its
stronger middle-register chords. Across 44.1, 48 and 96 kHz, the 72 reference
renders peak from **-7.799998 to -6.620924 dBFS**.
Main keeps its existing **−1 dBFS safety ceiling** for stronger accents.
Soft Yarn Nocturne and Kalimba Thumb Lullaby meet the same reference without
changing their physical voicing or the Volume law. Their former low-level
exceptions are removed.

## Fixed calibration and compatibility

`MainCalibration.h` stores four fixed gains, calibrated from each initialized
model's weakest reference peak across the three rates. Init retains Volume
0.85. Each factory patch then stores a separately solved Volume position; Etude
receives a further fixed −0.8 dB VCA trim after the stronger-playing check.
The Rack XML is the preset source of truth, and `Tools/export_plugin_tables.py`
regenerates the shared plug-in factory bank from those patches.

| Initialized model | Previous gain | Final gain | Increase before dynamics |
| --- | ---: | ---: | ---: |
| Rosewood | 1.225115902 | 2.156637999 | 4.912 dB |
| Padauk | 1.174502045 | 2.113276235 | 5.102 dB |
| Balafon | 1.406057112 | 2.805121587 | 5.999 dB |
| Kalimba | 1.034682015 | 2.111566469 | 6.196 dB |

Saved user settings keep their stored parameter values and receive the model's
increase above **before** musical Limit and output protection. Their output can
therefore be louder and, with Limit active or stronger notes, more compressed.
This is an intentional calibration change. The factory Volume adjustments
compensate that model increase to obtain the new shared phrase level.

Volume retains its square law, 85% unity point, 2.25× maximum and 5 ms smoothing.
The physical model, all other factory sound parameters, native 1× default,
Drive, musical Limit, stereo linking, 2 ms look-ahead, detector/recovery and
latency are unchanged. Direct Close, Room and Piezo retain their independent
fixed recording trims and 0.94 protection ceilings.

No runtime RMS matcher, automatic normalization or audio-file normalization is
used. Natural reference stereo RMS spans **-29.096 to -20.727 dBFS**;
the −7 dBFS figure is the normal reference's peak target, not an RMS target or
a claim that every key, chord or velocity has the same level. Peaks here are
sample peaks, not a certified true-peak mastering measurement.

## Measured before and after

These native player renders use the actual shared engine, control mapping and
authored bank. Before is 1.5.4; final is 1.5.5. Both use native 1×, Linear
velocity 80 and the five-second reference. Each table row is an independent
render with its actual final controls, not a normalized source file.

| State | Volume before → final | Main peak at 48 kHz, before → final (dBFS) | Final stereo RMS at 48 kHz (dBFS) |
| --- | ---: | ---: | ---: |
| Init model 0 | 0.85000000 → 0.85000000 | -11.880 → -6.968 | -22.087 |
| Init model 1 | 0.85000000 → 0.85000000 | -12.082 → -6.980 | -21.896 |
| Init model 2 | 0.85000000 → 0.85000000 | -12.992 → -6.993 | -20.988 |
| Init model 3 | 0.85000000 → 0.85000000 | -13.196 → -7.000 | -20.918 |
| Ambient Cathedral Bloom | 0.97358644 → 0.89596403 | -7.994 → -6.994 | -23.145 |
| Concert Grand Rosewood | 0.96615160 → 0.88230181 | -8.000 → -7.000 | -22.244 |
| Direct Piezo Mallet | 0.72057581 → 0.57528055 | -7.778 → -6.778 | -28.298 |
| Hard Acrylic Virtuoso | 0.70143330 → 0.55999780 | -7.997 → -6.997 | -28.878 |
| Rosewood Dampened Etude | 0.97453463 → 0.87332014 | -8.000 → -7.800 | -22.808 |
| Rosewood Roll Chorale | 0.98390567 → 0.91314423 | -7.929 → -6.929 | -22.444 |
| Soft Yarn Nocturne | 1.00000000 → 0.94893837 | -8.521 → -6.903 | -21.743 |
| Chiapas Folk Fiesta | 0.65041673 → 0.51361823 | -8.000 → -7.000 | -29.088 |
| Guatemalan Charleo | 0.75214517 → 0.59395075 | -7.996 → -6.996 | -25.841 |
| Mayan Dance Padauk | 0.91508961 → 0.75644875 | -7.969 → -6.969 | -22.640 |
| Ancestral Balafon | 0.74396729 → 0.55792964 | -7.999 → -6.999 | -26.588 |
| Birifor Gyil Buzz | 0.74793708 → 0.56090689 | -8.000 → -7.000 | -26.311 |
| Chopi Timbila Ensemble | 0.71368384 → 0.53521895 | -7.984 → -6.984 | -27.632 |
| Equipentatonic Log Keys | 0.76532996 → 0.57395041 | -8.000 → -7.000 | -29.011 |
| Mandinka Bala Heptatonic | 0.72506344 → 0.54375303 | -7.993 → -6.993 | -27.598 |
| Midnight Village Ritual | 0.87777889 → 0.66378796 | -8.000 → -7.000 | -23.556 |
| Sambla Talking Balafon | 0.72646320 → 0.54480278 | -8.000 → -7.000 | -27.666 |
| Kalimba Muted Thumbs | 0.98134434 → 0.87204790 | -7.989 → -6.989 | -21.185 |
| Kalimba Pick Sparkle | 0.96537077 → 0.82644165 | -7.988 → -6.988 | -20.967 |
| Kalimba Thumb Lullaby | 1.00000000 → 0.99150670 | -12.500 → -6.917 | -20.916 |

## Reproduction and evidence

The shared reference uses MIDI notes `60, 64, 67, 72, 67, 64, 60, 60, 62, 65,
69, 74, 69, 65, 62, 62, 48, 55, 36, 84`, 150 ms between onsets, 100 ms holds,
Linear velocity 80 and exactly five seconds including the tail. Stereo RMS is
`sqrt(sum(L² + R²) / (2 × frames))`; peak is the largest absolute Main sample.
Init and every preset use their native 1× default at 44.1, 48 and 96 kHz.

Fresh before renders establish each init peak. A fixed model-gain multiplier
`10^((-7 - weakest_peak_dbfs) / 20)` moves its weakest-rate peak to −7 dBFS.
Fresh quiet captures at Volume .2125 (gain .0625) then feed the existing
production-guard VCA solver:

```sh
MarembaMainCalibrationTests --quiet --rate 44100 --capture /fresh/quiet-44100
MarembaMainCalibrationTests --quiet --rate 48000 --capture /fresh/quiet-48000
MarembaMainCalibrationTests --quiet --rate 96000 --capture /fresh/quiet-96000
CalibrateMarembaPresetVca --target -7 /fresh/quiet-44100 /fresh/quiet-48000 /fresh/quiet-96000
```

The solver proposals target −7 dBFS for all 20 presets. The Rack ordinary-playing
matrix then found that Etude's velocity-100 middle chord exceeded the existing
gentle-protection bounds at that level. Its extra −0.8 dB trim preserves the
normal-play policy without changing timbre or dynamics. The solver CSV retains
the untrimmed proposal; the actual final VCA and renders include that trim.

The solver finds a passing 1/32 VCA grid interval and refines that interval;
it does not assert global monotonicity. Its 60 final reference replays have
**zero Main gain reduction**, and all 20 target crossings are reachable below
full Volume. Replay adds another guard delay and truncates that many extra
tail frames; the final levels above come from the independent actual-control
renders, not that estimate. The [solver CSV](playing-level-vca-solutions-2026-10-20.csv)
retains proposals, full-VCA reachability and reduction metrics.

## Native validation

**All required native cases pass.** The first candidate passed the full
216-case matrix: 24 states × three scenarios × three rates. After Etude's
VCA-only trim, all 72 normal reference cases were freshly rendered and passed.
The 144 stress cases retain their earlier results: they explicitly force Volume
to 1, and every other sound parameter and DSP input is unchanged by that trim.
The final CSVs distinguish these two origins in `measurement_origin`.

Normal references must stay within −7 ±0.4 dBFS peak, except Etude's explicit
−7.8 ±0.4 dBFS range, and each individual RMS anchor ±0.35 dB. Stress uses
Linear velocity 127 and full Volume, with either
the phrase two octaves lower or an eight-note low chord (MIDI 24, 28, 31,
36, 40, 43, 48, 52). Every Main sample remains below its −1 dBFS ceiling;
every direct sample remains below 0.940002. Nonfinite output, missing writes,
buffer overruns and silent Main renders are rejected.

Final per-case data: [44.1 kHz](playing-levels-44100-2026-10-20.csv),
[48 kHz](playing-levels-48000-2026-10-20.csv),
[96 kHz](playing-levels-96000-2026-10-20.csv).
All **432** direct-output peak/RMS fields in the 72 normal references match
the before measurements exactly. Separate `MainVolume` and `DirectOutputLevel`
contracts retain direct isolation and calibration checks.

Six focused contracts pass: `Controls`, `OneXQuality`, `MainVolume`,
`Headroom`, `DirectOutputLevel` and `MainPickupAlignment`. The latter's
independent gain oracle is intentionally updated to the four fixed gains;
its electrical-flight and routing bounds are unchanged. The headroom contract
retains its eight-rate protector, linked-stereo, quiet-delay and direct-group
coverage. Seven Python calibration-tool tests also pass. No dynamics algorithm
or waveform-processing test bound was changed.

The [calibration record](playing-level-calibration-2026-10-20.json) preserves
source/binary and CSV hashes, model-gain calculations, VCA proposals, Etude trim, regression
anchors and numerical summary. This is native source validation, not SDK
compilation, installation, host playback or CPU qualification. Broader ordinary
playing and its amount of protection are measured separately by the Rack
wrapper; the zero-reduction statement here applies to the declared reference
phrase only. Build, host and delivery evidence belong to the Rack port records.
