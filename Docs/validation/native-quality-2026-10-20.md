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

Only Equipentatonic Log Keys exceeded that RMS tolerance. Its factory Volume
changes from `0.92831682` to `0.90934947`. The
[fit trials](native-quality-equip-fit-2026-10-20.csv) measure the final candidate
at −26.9963 dBFS RMS and −7.000072 dBFS peak. The other 23 states remain within
0.171 dB of −27 dBFS. No model gain or other sound control changes were needed.
Previously saved patches retain their Volume and Quality values.

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
