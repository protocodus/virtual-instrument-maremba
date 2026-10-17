# Limiter listening comparison

These separate auditions compare the former gentle compressor with the new limiter. They use the same original note phrase, parameters and deterministic start. Each file plays **old for 5.75 seconds, silence for 0.5 seconds, then new for 5.75 seconds**.

Amount 0.25 represents the factory amount; Amount 1 demonstrates strong limiting. Both use Attack 12 ms and Release 120 ms. Factory-like uses a near-Concert-Grand Rosewood program; Driven uses a harder striker, all microphone levels at 1, Drive 0.75 and Volume 1. The four single notes rise from MIDI velocity 32 to 127, followed by forte chords and soft notes.

Raw measurements below precede PCM conversion, playback gain and the final 40 ms presentation fade. There is no makeup gain or loudness matching. A single attenuation, never a boost, is shared by both versions and both amounts within each program; it also allows headroom for its dry reference. The existing acoustic demo collection is unchanged.

| Program | Amount | Old peak | New peak | Old RMS | New RMS | Playback gain | Listen |
|---|---:|---:|---:|---:|---:|---:|---|
| factory_like | 0.25 | -3.29 dBFS | -4.66 dBFS | -19.99 dBFS | -21.63 dBFS | 0.00 dB | [AAC](../../Tools/Demos/Audio/limiter_comparison/factory_like_0.25_before_after.m4a) · [WAV](../../Tools/Demos/Audio/limiter_comparison/factory_like_0.25_before_after.wav) |
| factory_like | 1 | -4.12 dBFS | -7.69 dBFS | -24.09 dBFS | -29.56 dBFS | 0.00 dB | [AAC](../../Tools/Demos/Audio/limiter_comparison/factory_like_1_before_after.m4a) · [WAV](../../Tools/Demos/Audio/limiter_comparison/factory_like_1_before_after.wav) |
| driven | 0.25 | 0.64 dBFS | -1.63 dBFS | -9.68 dBFS | -12.04 dBFS | -3.94 dB | [AAC](../../Tools/Demos/Audio/limiter_comparison/driven_0.25_before_after.m4a) · [WAV](../../Tools/Demos/Audio/limiter_comparison/driven_0.25_before_after.wav) |
| driven | 1 | -0.18 dBFS | -6.96 dBFS | -13.08 dBFS | -22.97 dBFS | -3.94 dB | [AAC](../../Tools/Demos/Audio/limiter_comparison/driven_1_before_after.m4a) · [WAV](../../Tools/Demos/Audio/limiter_comparison/driven_1_before_after.wav) |

Exact comparisons use unshifted float samples: Amount 0 reproduces all seven outputs bit-for-bit; every direct stem is identical across versions and amounts. All processed onsets begin on the same sample as their dry reference. This is a sample-alignment check on these phrases, not a frequency-dependent group-delay measurement.

Post-accent window RMS ratios in the JSON include ringing tails and following notes; they illustrate recovery in music, not an isolated measurement of the detector's release constant. Dedicated limiter tests establish ceiling and ballistics contracts. A sample-peak bound does not establish true-peak limiting.

Baseline DSP: `7b001722c19ad5bf7dab252788a38b6d59964fe9`. All source, binary, raw-audio and delivery hashes are in [the machine-readable report](limiter-comparison.json). Audio and compiler caches are local, ignored files.

Reproduce from Maremba: `python3 Tools/Demos/compare_limiter.py`. Requires clang++ and ffmpeg. `--prepare-baseline` captures only the hash-verified old engine renders.
