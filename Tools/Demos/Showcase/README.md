# Maremba — four small worlds

Original release showcases for **1.4.0f3**. Open `Maremba_Showcase.html`: all four AAC previews are embedded, so it works offline as a single file. The adjacent WAVs are the 48 kHz, stereo, 24-bit delivery masters. AAC previews use 192 kbit/s.

| Instrument / piece | Length | Notes / velocity | WAV LUFS / dBTP | AAC dBTP | Final 0.5 s RMS |
|---|---:|---|---|---:|---:|
| Imperial Rosewood — Afterglow | 27.30 s | 71 / 46–83 | -26.7 / -14.2 | -14.2 | -102.1 dBFS |
| Mayan Padauk — Amber Steps | 22.40 s | 82 / 44–85 | -22.5 / -7.7 | -7.7 | -102.1 dBFS |
| Balafon Ancestral — Sun Weave | 21.60 s | 81 / 42–91 | -18.0 / -5.5 | -3.4 | -103.4 dBFS |
| Kalimba Artisan — Firefly Window | 26.70 s | 61 / 37–78 | -28.6 / -15.0 | -15.0 | -99.0 dBFS |

Each piece has its own harmonies, register, phrase shape, deterministic timing and velocity variation. The engine supplies contact and overtone variation. These are short original compositions, not adaptations of third-party repertoire. No samples or external instruments were used.

The supplied `.mid` files and `.events` contain the same performance timing (960 ticks per quarter, with the exported tempo); all parameters are explicit in `.parameters.json` and `.parameters.txt`. Note-offs are retained for normal MIDI use, although Maremba lets its bars and tines ring freely. Rendering initially reserves 6–8 seconds for decay and extends in four-second steps when needed, up to a 35-second track limit. Delivery silence is trimmed only after the last sample above −90 dBFS, retaining at least another 600 ms and a minimum track length of 20 seconds. The 250 ms endpoint fade starts after that threshold. All four pieces use the same **-7.68000 dB** constant gain: the loudest piece targets −18 LUFS, constrained by a −2 dBTP ceiling across the complete set. No individual normalization is applied, preserving the engine's relative instrument levels and dynamics. Buzz is 0%, Drive is 1%, and the built-in limiter and external processing are disabled. The built-in room differs by instrument.

The renderer compiles the actual checked-out DSP files, runs at 48 kHz with 4× internal oversampling and at most 64 frames per call, and splits calls at every exact note frame. The manifest hashes the complete DSP source set, adapter, compiler output, MIDI, events, parameters and audio. It also records the checked-out Git HEAD; the DSP hashes are authoritative if the worktree contains uncommitted source changes.

DSP set SHA-256: `09a8e2a1129239b79995e15ac5fc89c1d065e4951fd6f4c51ac41dde2f4abba1`

Repository HEAD at rendering: `a62c8c3ec89689f0ff315f58c26a1461f2a9aa5e`

Validation: every raw engine sample and decoded PCM/AAC sample is finite. Decoded previews and delivery masters have no clipped samples; true peaks and full decay tails were measured with ffmpeg. The DSP source set was unchanged before/after rendering. Objective checks are recorded; no model-accessible listening tool was available, so subjective audition is not claimed.

Rebuild from the current checkout:

```sh
python3 Tools/Demos/Showcase/build_showcase.py --output Release/validation/1.4.0f3/media/Showcase
python3 Tools/Demos/Showcase/validate_showcase.py --input Release/validation/1.4.0f3/media/Showcase
```

Requires Python 3 with NumPy, clang++ and ffmpeg. `--compose-only` regenerates the original MIDI/events/parameter files without rendering. `--output` keeps previous delivered audio untouched. Existing demos outside that output folder are preserved.
