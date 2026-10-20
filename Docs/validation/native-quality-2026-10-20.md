# Native-rate quality — 2026-10-20

Maremba 1.5.3 adds a true native-rate path and uses it for initialized devices
and all 20 factory presets. The physical engine runs once per host frame;
mono and stereo decimators forward the sample exactly, without filtering or
additional latency. CaptureScene selects its one-sample source path explicitly.

Stored selector indices remain `0=2x`, `1=4x`, `2=8x`; `3=1x` is appended.
The Rack selector therefore has four steps in that order. Raw Rack and plug-in
state values retain their factors. External JUCE automation saved as normalized
values needs review because a fourth choice changes normalized step positions.
Native rate can expose more aliasing than the higher quality settings.

## Output calibration

The [existing protocol](output-calibration-2026-10-20.md), fixed model gains,
Volume law, −7 dBFS Main ceiling, direct-output protection and 2 ms look-ahead
remain unchanged. The five-second, velocity-80 Linear reference phrase was
measured for all four initialized models and all 20 factory presets at 48 kHz.
The [initial measurements](native-quality-before-trim-48000-2026-10-20.csv)
span −27.171 to −26.469 dBFS stereo RMS. The highest Main peak is −7.000009 dBFS.
Measurement mode reports the RMS rather than asserting the target; its safety
checks still run. These numbers were compared with the existing ±0.35 dB limit.

Only Equipentatonic Log Keys exceeded that RMS tolerance. An initial
[48 kHz fit](native-quality-equip-fit-2026-10-20.csv) selected `0.90934947`,
but the [three-rate check](native-quality-equip-three-rates-before-2026-10-20.csv)
then measured −27.3894 dBFS at 96 kHz, outside the unchanged tolerance.
Its final factory Volume changes from `0.92831682` to **`0.91707000`** to
balance all three rates. The [final measurements](native-quality-equip-three-rates-final-2026-10-20.csv)
give −26.766, −26.787 and −27.232 dBFS RMS at 44.1/48/96 kHz. An independent
Rack-wrapper render of the actual final XML confirms these values, with maximum
target error 0.234 dB and Main phrase peaks below −7 dBFS at all three rates.
The other 23 states retain their controls and the measured 48 kHz RMS within
0.171 dB of −27 dBFS. No model gain or other sound control changes were needed.
Previously saved patches retain their Volume and Quality values.

## Paired engine processing cost

`OneXQualityTest --benchmark` compares native 1x and legacy 2x with sixteen
retained, nonfading voices, all seven outputs, 64-frame blocks and 512 measured
blocks after warmup. It takes the median thread CPU time from three alternating
pairs for every model at 44.1 and 48 kHz. The
[eight results](native-quality-cpu-2026-10-20.csv) use 76.90–78.92% as much
thread CPU at 1x, a 21.08–23.10% reduction. Physical factor, retained voices and
audible finite output are asserted. Unrelated rendering workloads were active;
this relative source-engine comparison does not qualify hosted realtime deadlines.

## Focused validation

- `OneXQualityTest`: exact mono/template/stereo bypass, signed zero, unchanged
  hidden filter state, all four physical factors, every ordered live quality
  transition and queued strike, Main/Close transfer and alignment, and native
  velocity-127/full-Volume wide chords at 22.05/44.1/48 kHz. Engine clock checks
  reject silent emergency resets during the safety fixture.
- `CaptureSceneEqualityTest`: 195 fixtures across 13 rates and four factors;
  7,192,679 checks passed, including an independent native source oracle.
- `CloseEngineArrivalTest`: 90 cases and 3.73 million checks passed, including
  native-rate physical arrival at 22.05/44.1/48 kHz. Worst transfer error is
  0.0842 dB and phase error 0.00593 radians within unchanged bounds.
- `AcousticReadoutTest` includes native-rate hard-strike readout;
  `MainPickupAlignmentTest` passed 4.52 million checks.
- Controls, queued note release, frame/body transitions, the discrete parameter
  matrix and demo CLI contracts passed with the appended quality state.
- Engine quality checks passed all four factors/models, pitch captures (worst
  0.0315 cents), unchanged ±0.5 dB rate-invariance limits at 44.1/48/96/192 kHz,
  live switches and Main/direct linear residuals from −126.8 to −130.2 dB.
- Compact engine fuzz passed with zero failures. Historical physical reference
  fixtures explicitly retain their original 2x factor; new default, factor,
  live-transition and native-rate fixtures cover the new behavior separately.

These are source-engine contracts. SDK module and Recon results belong to the
Rack build record; these checks do not qualify Reason scheduling or playback.

The first full Rack preflight stopped at the hard-hit matrix's former 900 s
wall-time hang guard under concurrent rendering load, after the 480-case
progress marker. It did not report an audio-assertion failure. The expanded
700-case matrix now allows 3600 s (7000 s under sanitizers), with matching
CTest outer limits. Every case, rendered duration, output limit and reset guard
is retained. The failed invocation is preserved in the Rack build diagnostics;
a fresh complete preflight is required for the installable build.

The second preflight passed all 700 hard-hit cases, including 100 native-rate
chords, with no ceiling/full-scale violations. It exposed a test-reference
mismatch in `Round3ResponseTest`: the copied native-rate voice and mesh were
advanced twice per host frame. The reference now uses an independent saved
selector/factor table and checks both native 1x and historical 2x. All 88
register fixtures pass the original `2e-4` scene-residual bound. No DSP or
output limit changes were needed. The failed invocation is preserved as well.

The remaining 18 physical contracts pass focused checks. Their audit corrected
two more reference assumptions: Round4's copied voice now advances its envelope
every `factor * 16` internal samples, and BuzzRange's copied buzzer uses the
original 96 kHz probe's physical frequencies and 42.67 ms duration at every
internal rate. Both explicitly cover legacy 2x and native 1x. Exact response
comparisons, positive-energy witnesses and all numeric bounds are retained.
