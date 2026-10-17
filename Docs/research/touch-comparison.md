# Maremba controlled touch comparison

Baseline: main `5438b104887ae3743bec26a5e87adb06dd549371`. Current source SHA-256 map is in [touch-comparison.json](touch-comparison.json).

Reproduce with `python3 Tools/Demos/compare_touch.py`. The script restores and builds the pinned baseline when needed; `--capture-baseline` only captures it. All source snapshots, binaries and probe/A-B audio live in ignored `Tools/Demos/Audio/TouchBaseline` and `Tools/Demos/Audio/TouchCurrent`.

Forty-eight three-second single-note C4/C6 main-bus renders per version use identical note timing, gain, model, striker and position. Strikes start at 50 ms; Variance and Artifacts are zero. The current engine retains its subtle intrinsic strike variation at these settings. Compression/drive remain off, while each model retains its documented demo microphone/body settings. These are end-to-end listening probes; isolated raw-voice tests (the Rack device's Docs/research/touch-response.md) provide separate spectral evidence.

RMS uses both stereo channels. Attack is the first 20 ms after the note event, body is 20–200 ms, and late decay is 600–1500 ms. Measurements include the oversampler and microphone delays. Comparisons use exported PCM without loudness matching. Every export gain is recorded so any safety attenuation remains visible. Measured differences establish changed response, not perceptual equivalence to an acoustic instrument.

## Quiet-to-loud body dynamic span

The span is body RMS at velocity 127 minus velocity 16, at centre position with the medium/natural striker.

| Model | Baseline span | Current span | Current pp body | Current ff body |
|---|---:|---:|---:|---:|
| Rosewood | 22.47 dB | 23.00 dB | -37.56 dBFS | -14.56 dBFS |
| Padauk | 22.44 dB | 22.90 dB | -40.10 dBFS | -17.20 dBFS |
| Balafon | 22.53 dB | 22.94 dB | -41.29 dBFS | -18.35 dBFS |
| Kalimba | 22.00 dB | 22.06 dB | -40.10 dBFS | -18.04 dBFS |

## C6 register comparison

Same controls and velocities 16/127, two octaves above C4. The separate raw-voice report measures spectral/contact changes; these values document the delivered main-bus energy.

| Model | Baseline pp / ff body | Current pp / ff body | Current span |
|---|---:|---:|---:|
| Rosewood | -44.56 / -22.88 dBFS | -44.95 / -20.79 dBFS | 24.16 dB |
| Padauk | -47.90 / -26.23 dBFS | -48.23 / -24.12 dBFS | 24.11 dB |
| Balafon | -50.15 / -28.48 dBFS | -50.41 / -26.29 dBFS | 24.12 dB |
| Kalimba | -46.97 / -22.57 dBFS | -45.11 / -21.68 dBFS | 23.44 dB |

## Position and pluck differences

Fixed velocity 90. Negative differences indicate lower energy in that window, not lower quality. A quieter bar fundamental near its node is an intended spatial behavior. Kalimba percentages are control positions along the playable end, mapped to 65–100% of physical tine length, not the whole clamp-to-tip distance.

| Model / control | Attack change | Body change | Late-decay change |
|---|---:|---:|---:|
| Rosewood / position 10% | +0.15 dB | +0.15 dB | +0.14 dB |
| Rosewood / position 22.42% | +1.54 dB | +1.55 dB | +1.88 dB |
| Rosewood / position 35% | +0.16 dB | +0.15 dB | +0.14 dB |
| Rosewood / position 50% | +0.15 dB | +0.15 dB | +0.14 dB |
| Padauk / position 10% | +0.15 dB | +0.14 dB | +0.14 dB |
| Padauk / position 22.42% | +1.52 dB | +1.53 dB | +2.12 dB |
| Padauk / position 35% | +0.16 dB | +0.14 dB | +0.14 dB |
| Padauk / position 50% | +0.15 dB | +0.14 dB | +0.14 dB |
| Balafon / position 10% | +0.15 dB | +0.15 dB | +0.14 dB |
| Balafon / position 22.42% | +1.49 dB | +1.51 dB | +1.96 dB |
| Balafon / position 35% | +0.16 dB | +0.14 dB | +0.14 dB |
| Balafon / position 50% | +0.15 dB | +0.14 dB | +0.14 dB |
| Kalimba / position 10% | +0.02 dB | +0.02 dB | +0.01 dB |
| Kalimba / position 35% | +0.01 dB | +0.01 dB | +0.01 dB |
| Kalimba / position 50% | +0.01 dB | +0.01 dB | +0.01 dB |
| Kalimba / position 90% | +0.02 dB | +0.02 dB | +0.01 dB |
| Kalimba / pluck 0 | +0.08 dB | +0.08 dB | +0.08 dB |
| Kalimba / pluck 1 | +0.01 dB | +0.01 dB | +0.01 dB |
| Kalimba / pluck 2 | -0.05 dB | -0.04 dB | -0.04 dB |
| Kalimba / pluck 3 | -0.35 dB | -0.34 dB | -0.34 dB |

## Local A/B auditions

Each file plays its probes from the baseline, one second of silence, then the current version in the same order. Dynamics use velocities 16, 32, 64, 96, 127; Kalimba plucks use flesh, natural thumb, thumbnail and pick. Register files play C4 pp, C4 ff, C6 pp, C6 ff. Each hit has three seconds to ring. No gain matching is applied. The following files are generated locally and are not committed:

- [Rosewood_dynamics_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Rosewood_dynamics_before_after.m4a)
- [Padauk_dynamics_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Padauk_dynamics_before_after.m4a)
- [Balafon_dynamics_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Balafon_dynamics_before_after.m4a)
- [Kalimba_dynamics_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Kalimba_dynamics_before_after.m4a)
- [Kalimba_plucks_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Kalimba_plucks_before_after.m4a)
- [Rosewood_register_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Rosewood_register_before_after.m4a)
- [Padauk_register_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Padauk_register_before_after.m4a)
- [Balafon_register_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Balafon_register_before_after.m4a)
- [Kalimba_register_before_after.m4a](../../Tools/Demos/Audio/TouchCurrent/Kalimba_register_before_after.m4a)

All individual WAVs and decoded A/B AAC files passed finite/non-clipping checks. The JSON records input/source/media SHA-256 digests and per-probe renderer/export metrics.
