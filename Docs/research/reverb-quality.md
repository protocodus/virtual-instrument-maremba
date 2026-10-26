# Maremba room quality — 1.4.0f7

Version **1.5.0f1** carries this reverb implementation unchanged. The
measurements below describe the original 1.4.0f7 sound update.

The production RoomCapture now gives Wood and Chamber smoother late spectra
and Hall a denser onset. This intentionally changes the reverb sound. The
physical direct sound, six first-order wall reflections, microphone models,
room selector, 50 ms transition and host latency remain unchanged.

## Algorithm

Each room retains its two twelve-line feedback delay networks and original
slow orthogonal scattering motion. Wood and Chamber use diffuse loop paths
2.5 times their former lengths, increasing modal density. Hall retains its
main loop lengths. Every loop adds a short lossless Schroeder allpass with
fractional Thiran timing. Coefficients are bounded by 0.35; additional maximum
group delay stays below 3.95% of the main loop. Existing wall-dependent decay
fitting and energy calibration follow the revised main lengths. The input
diffusers and first late arrival are unchanged.

This original implementation uses the established principle of
[scattering filters in feedback delay networks](https://arxiv.org/abs/1912.08888),
with short bounded loop filters rather than copying that paper's full matrix
design. The existing
[time-varying orthogonal matrix](https://doi.org/10.1121/1.4928394)
is retained. Rendering and reset allocate no memory; storage is reserved at
construction. Legacy RoomBank voicings leave loop diffusion disabled and remain
bit-identical in the comparison fixture.

## Measurements

The probe compiles unchanged production headers. It measures the six early
reflections and both late networks through the actual Room microphones, with
only the direct tap excluded. Identical 0.25-pressure impulses are used before
and after; there is no normalization of the measured response.

| 48 kHz measurement | Before | f7 |
| --- | ---: | ---: |
| Wood early spectral roughness, L / R | 14.79 / 14.18 dB | 8.84 / 8.47 dB |
| Chamber early spectral roughness, L / R | 11.82 / 10.93 dB | 7.27 / 6.99 dB |
| Hall early normalized echo density, L / R | 0.860 / 0.871 | 0.933 / 0.923 |
| Wet impulse energy change, Wood / Chamber / Hall | reference | −0.11 / −0.34 / +0.11 dB |

Lower roughness means fewer narrow spectral peaks; density near one is a
noise-like local echo distribution. These are useful acoustic measures, not a
substitute for listening. Constant-gain RMS-matched before/after phrase clips
are retained locally with a manifest in the
[validation record](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/reverb-quality-2026-10-06/README.md).

All nine room/rate cases pass at 44.1, 48 and 96 kHz. The gate covers early and
late spectral roughness, two echo-density windows, stereo correlation, mono
fold-down, and decay fits at 125 Hz, 500 Hz, 4 kHz and 10 kHz. Decay must remain
within 12% of the wall-derived target, with fit R² at least 0.95. The former
production room fails 33 of these density/roughness conditions as a negative
control. The exact bounds and measurements are retained in the report.

Reproduce from the device directory:

```sh
python3 Tests/measure_room_quality.py --output /tmp/maremba-room-quality-new --check
```

The mandatory build suite also runs the allpass passivity/reset/allocation
contract, production RoomCapture and motion contracts, full engine and Rack
wrapper checks, and the separate [output safety checks](output-level.md).
The room measurement deliberately excludes the new global 70% output gains.

## Performance and delivery scope

A paired native eight-note phrase benchmark with identical non-room DSP found
median total thread CPU rising 15.2%; active-block p50 rose 10.4% and p99 rose
9.1%. It uses 48 kHz, 64-frame blocks and default 2× quality. The validation
record also records two paired runs of the dense 16-voice workload at the same
rate, block size and quality. Median thread CPU rose 3.31% in Wood, 2.63% in
Chamber and 2.78% in Hall, to approximately 633, 627 and 623 µs per block.
Candidate p99 was 708–713 µs against a 1,333 µs block budget. Sporadic deadline
outliers occurred in both baseline and candidate and remain in the report;
these are observations, not a guarantee of realtime performance. These
machine-local measurements exclude Reason scheduling and target translation.

SDK build and package status is recorded in the [device README](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/README.md).
Interactive Reason playback and host CPU remain unverified in this shared
profile; native measurements must not be presented as host qualification.
