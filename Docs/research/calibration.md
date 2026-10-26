# Calibrating Maremba against recordings

Two SDK-free tools in `Instrument/Tools/Demos/` turn recordings of real instruments (concert marimba,
Guatemalan marimba, balafon, kalimba) into targets for the model:

- `fit_modal_reference.py` measures each strike's partials: ratio, T60, loss factor,
  onset amplitude, level 20 and 100 ms after the stroke, resonator bloom and attack
  brightness. It also diffs a recording against a model render of the same note.
- `build_listening_test.py` builds a self-contained blind ABX or MUSHRA page that
  checks whether the fitted model is heard as the instrument.

Both need Python 3.11 with numpy and scipy only; ffmpeg is optional (AAC pages).
`python3 Tests/test_calibration_tools.py` checks both in about 10 s.

## 1. Recording references

| Item | Practice |
| --- | --- |
| Keys | Every key if possible; at least every third semitone plus the lowest and highest. |
| Velocity | ≥ 5 levels, pp to ff (`v1`–`v5`), one strike per take, two takes each. |
| Striker | 2 mallets, e.g. medium cord and hard rubber; kalimba: thumb flesh and nail. |
| Position | 2 strike positions: bar centre (`ctr`, a node of the 4:1 mode; Maremba Position 0.5) and a third of the way from centre to end (`off`, all low modes; Position ≈ 0.67); kalimba: near the tip (`tip`) and mid-tine (`mid`). |
| Mics | Close (15–25 cm above the struck bar or tine, `close`) and far (1.5–2 m, `far`), recorded simultaneously, one file per mic. |
| Room | Dry and quiet: noise floor ≥ 60 dB below the ff peak; no heating fans. |
| Format | 24-bit / 96 kHz WAV, no normalising, fades, EQ or limiting; ff peaks near −6 dBFS. |
| Pre- and post-roll | ≥ 0.5 s of room tone before the strike (the tool measures the noise there); let the note decay into the noise (4–8 s low, 2 s high). |
| Isolation | Damp neighbouring bars for the modal takes. For 3–5 keys per instrument, add a take with the resonator blocked (foam in the tube or gourd mouth): it separates bar loss from resonator radiation. |
| Log | A CSV per session: tuning reference (A = 440/442/443), mallet model, mic model and distance, temperature and humidity (wood tuning drifts with both). |

Slate by file name, not by voice; the tools read the note from the name:

```
<instr>_<note>_<vel>_<striker>_<pos>_<mic>_t<take>.wav     e.g. ROS_C4_v3_cord_off_close_t1.wav
instr: ROS concert marimba · GUA Guatemalan marimba · BAL balafon · KAL kalimba
note:  scientific pitch, C4 = MIDI 60 = 261.63 Hz; sharps as s (Fs3), flats as b (Bb2)
```

## 2. Fitting a recording

```bash
python3 Instrument/Tools/Demos/fit_modal_reference.py rec/ROS_C4_v3_cord_off_close_t1.wav --markdown
python3 Instrument/Tools/Demos/fit_modal_reference.py rec/ROS_*_close_*.wav --json fits/rosewood_close.json
python3 Instrument/Tools/Demos/fit_modal_reference.py take.wav --note Fs3      # or --note 54, --note 184.5Hz
python3 Instrument/Tools/Demos/fit_modal_reference.py --self-test              # synthetic strikes, exits 1 on failure
```

Options: `--max-partials` (12), `--fmax` (16000 Hz), `--windows 0.02-0.12,0.05-0.4,0.2-end`,
`--onset SECONDS` when the energy-rise detector picks the wrong event.

How it measures: the post-onset signal is cut into flat-top bands with Gaussian skirts
(each band kernel stays inside its window) and decimated. ESPRIT then fits damped
exponentials per band in three overlapping windows, and each partial keeps the window
whose Cramér–Rao bound on the damping is smallest. Fast high partials therefore come
from 20–120 ms and long low ones from 200 ms to the end. Poles need a 15 dB
matched-filter SNR, positive damping, a bandwidth under 30 % of their frequency, a
level within 80 dB of the strongest partial and ≤ 40 dB of implied decay before their
window. Weaker poles within half an ERB (≤ 40 Hz) of a partial at least 6 dB
stronger describe its envelope (bloom, beating); they are listed as its `sub_poles`.

The self-test covers four synthetic strikes (marimba 1:4:10 with T60 3/0.8/0.3 s;
kalimba 1:5.9 with a 0.35 s overtone; balafon 1:3.1:7.4 under a gated harmonic buzz
comb; marimba with a resonator bloom). Each has white noise at −60 dB re peak and is
written as 16-bit, 24-bit or float WAV. It asserts frequency within 0.2 %, T60 within
10 %, onset amplitude and 20/100 ms levels within 1.5 dB, bloom peak time within 10 %,
onset within 1 ms and the noise floor within 3 dB.

### JSON fields (one object per file)

| Field | Meaning |
| --- | --- |
| `note` | Nominal note: `name`, `midi`, `nominal_hz`, `source` (file name or `--note`). |
| `f0_hz`, `f0_cents_re_nominal` | Measured fundamental: the strongest partial within 60 cents of the note. |
| `partials[]` | Strongest partials by energy 20–220 ms after the onset, sorted by frequency. `rank` is the strength order. |
| `.freq_hz`, `.freq_std_hz`, `.ratio` | Frequency, its Cramér–Rao standard deviation, and ratio to `f0_hz`. |
| `.t60_s`, `.sigma_per_s`, `.t60_re_f0` | Decay: T60 = 6.91/σ for amplitude e^(−σt); T60 relative to the fundamental's. |
| `.eta` | Loss factor η = σ/(π f) = 2.2/(T60 · f). |
| `.amp`, `.amp_dbfs`, `.phase_rad` | Complex amplitude extrapolated to the onset for x(t) = A e^(−σt) cos(2πft + φ); 1.0 = full-scale sine. |
| `.level_20ms_db`, `.level_100ms_db` | Level re the fundamental at 20 and 100 ms, from the fitted early-window model (bloom and beating included). |
| `.snr_db`, `.t60_rel_std` | Matched-filter SNR and relative Cramér–Rao bound of T60 in the reporting window. |
| `.window`, `.per_window[]`, `.sub_poles[]` | Reporting window, estimates from every window (a changing T60 means a two-stage decay), envelope poles. |
| `bloom` | Fundamental's heterodyne envelope: `peak_ms`, `peak_db_re_20ms`, `envelope_db_re_peak` at fixed times, `resolution_ms` (kernel σ), and `rise_tau_ms`/`equivalent_q` (see §4). |
| `attack` | Spectral centroid of the first 20 ms (`centroid_hz`, `centroid_re_f0`). |
| `noise_floor` | `rms_dbfs`, `db_re_peak` and `method` (pre-roll RMS, or quietest tail frames as an upper bound). |
| `metadata`, `warnings` | Format, onset, end, digital silence, windows used, file SHA-256; clipping and note warnings. |

## 3. Comparing with the model

Render the same note, velocity, striker and position dry, i.e. Close only, with Room,
Sympathetic and Piezo at 0 (from Reason, or a renderer build with those settings).
`Instrument/Tools/Demos/maremba_renderer` keeps its fixed per-model mix with room and sympathetic halo,
which lengthens T60s, and strikes at the centre unless given `--strike-position`: a
centre stroke leaves out the 4:1 mode. Then:

```bash
python3 Instrument/Tools/Demos/fit_modal_reference.py rec/ROS_C4_v3_cord_off_close_t1.wav --compare render_C4.wav --markdown
python3 Instrument/Tools/Demos/fit_modal_reference.py fits/ref_C4.json --compare-json fits/model_C4.json
```

The table matches partials by ratio (±50 cents) and lists Δcents, ΔL20, ΔL100 and the
model/reference T60 ratio, plus f0, bloom and attack centroid. Model partials with no
match (room modes, sympathetic keys) are listed separately.

## 4. From fitted values to model parameters

The loss law is in `DSP/MarimbaModel.h` (`GetMaterialLoss`, `LossFactor`): each mode
m of frequency f_m decays with T60_m = 2.2 / (η(f_m) · f_m), scaled by the key's
fixed scatter and by Decay^`decayExponent`, where
η(f) = η0 + η2 · (f / 1 kHz)² + ηLow · (fLow / f)². A tine's fundamental uses
`TineFundamentalLossFactor`; the part above the steel's own loss leaves through the
bridge, each mode in proportion to its bridge motion (`KalimbaModes.h`).

| Measurement | Model parameter | Procedure |
| --- | --- | --- |
| Bending-mode `ratio` per key | Carving targets in `Tests/generate_bar_modes.py` (per-register targets for rosewood and hormigo, `balafon_first_overtone` for the balafon), then regenerate `DSP/BarModes.h`; kalimba mount stiffness (`Tests/generate_kalimba_mount.py`, `KalimbaModes.h`) | Median over velocities and takes per key; the spread across keys sets the `GetKeyVariation` tuned/untuned scatter. |
| `eta` of every partial of a blocked-resonator take | `MaterialLoss` η0, η2, ηLow, fLow | Fit η(f) over all partials and keys: η0 from the flat part below 2 kHz, η2 from the rise above it, ηLow only if the lowest bars lose more than the law predicts. |
| `bloom.peak_ms`, `peak_db_re_20ms`, the fundamental doublet in `sub_poles`, the sounding fundamental's T60 | `ResonatorProfile` (`qAtC4`, `qSlope`, `qCornerHz`, `coupling`, `tuneBassCents`/`tuneLowCents`/`tuneHighCents`) and the model's `airGain` | Peak time follows Q and coupling (near critical coupling gives one bloom, above it a beating doublet); the rise follows the mouth readout against the bar's dipole radiation; the doublet's spacing follows coupling and tuning. The voice tunes each bar against its resonator, so check the heard pitch with `Round5ResponseTest` after any change. |
| `attack.centroid_re_f0` and upper-mode `level_20ms_db`, per velocity and striker | `MalletSpec` (head mass, stiffness, exponent, core, hysteresis) per striker; Hardness scales the stiffness by 10^(1.1 (h − 0.5)) | A half-sine force of duration τ has spectrum ∝ \|cos(πfτ)/(1 − (2fτ)²)\|, first null at 1.5/τ; fit τ against velocity per striker (Hertz: τ ∝ v^−(p−1)/(p+1)) and check the rigid-wall times in `MalletContactTest`. |
| Kalimba overtone and 2 × f1 line T60 re the fundamental | Mount `bridgeLoss` per mode (`KalimbaModes.h`), `TineFundamentalLossFactor`, `KalimbaBodyLoading` | Set the bridge share so the rendered ratio matches; notes on the box's air resonance die sooner. |
| Near-integer-ratio partials with short T60 at high velocity (balafon, Guatemalan) | Membrane voicing (`MembraneVoicing` in `MarembaVoice.cpp`, `SlackMembrane.h`) | Not modal: use them only to place the buzz formant and threshold, then confirm by listening. |

## 5. Blind listening tests

```bash
python3 Instrument/Tools/Demos/build_listening_test.py --trials trials.json --mode mushra --seed 11 --out mushra.html
python3 Instrument/Tools/Demos/build_listening_test.py --trials trials.json --mode abx --abx-repeats 10 --out abx.html
python3 Instrument/Tools/Demos/build_listening_test.py --trial rec_C4.wav maremba=render_C4.wav --out quick.html
python3 Instrument/Tools/Demos/build_listening_test.py --demo --out demo.html   # Demos/Reference + filtered variants
```

A trial has a reference, one or more candidates and a label for each that listeners
never see. The builder puts every clip of a trial on one rate (48 kHz), one channel
count and one onset, with 30 ms pre-roll, a common length and a 50 ms fade. It then
normalises each clip to −23 LUFS (BS.1770 K-weighting with 400 ms gating) and lowers
the whole trial if a peak would pass −1 dBFS. MUSHRA adds a hidden reference and a
3.5 kHz low-pass anchor. ABX builds one trial per candidate and repeat, with random A/B
and X. Trial order and all draws come from `--seed`. Audio is AAC when ffmpeg is found,
else 16-bit WAV (`--format wav` forces it). Every button has its own dithered encoding,
so no two share identical data.

Send only the `.html`. `<name>.report.json` beside it holds the loudness, gains, peaks
and label key. Listeners export a results JSON: answers, what they played and when,
labels, and a summary (MUSHRA mean/SD per condition, hidden-reference ratings below 90;
ABX correct/trials with a one-sided binomial p). Answers changed after an export are
flagged. Labels are only obfuscated in the page, not encrypted: for adversarial
settings, supervise the session. Plan 10–16 ABX repeats per pair (16 trials need 12
correct for p ≈ 0.04). In MUSHRA, drop listeners who rate the hidden reference below
90 in more than 15 % of trials (BS.1534).

## 6. What the bundled references show

`Instrument/Tools/Demos/Reference` holds MP3-decoded General MIDI samples, which exercise the tools but
are poor calibration targets. They end in digital silence at 0.6–1.9 s, their codec
floor sits 56–72 dB below the peak, and FluidR3's 4:1 partial decays exactly with its
fundamental (T60 ratio 1.00), the signature of a looped sample under a soundfont
envelope. The MusyngKite marimba still behaves like a real instrument:

- A log–log fit of the fundamental T60 over the nine notes gives f0^−0.54 and 1.4 s at
  C3; the notes scatter around it (G3 2.5 s, C4 0.70 s, E4 0.49 s, G4 1.03 s, C6
  0.43 s), more than an instrument's keys do. The model's bare bars follow Hill's
  measurements instead (about 1/f, 4.0 s at C3); with their tubes, a dry render of
  the default patch sounds 1.56 s at G3, 1.12 s at C4 and 0.37 s at C6.
- 4:1 partials ring for 0.19–0.40 of the fundamental's T60 (C3: 0.68), and 10:1
  partials for 0.13–0.28.
- The bloom peaks at 153 ms (C3), 105 (G3), 76 (C4), 54 (G4), 37 (C5) and 14 ms (C6),
  close to 1/f0, as for a constant-Q resonator, and rises 5.7–8.8 dB above the 20 ms
  level up to C4. The fundamental often splits into a bar/air doublet, e.g. C3 at
  1.000 and 1.008 × f0.
- The samples are tuned about +11 cents (A ≈ 443 Hz), and the attack centroid stays
  near 0.7–1.5 kHz whatever the note.
