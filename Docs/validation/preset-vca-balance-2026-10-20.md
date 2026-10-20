# Preset VCA balance and dynamic headroom — 2026-10-20

Source candidate: **Maremba 1.5.4**, Rack adapter **1.5.4d1**.
The normal-playing target is around **−8 dBFS Main peaks**, with Volume
(VCA) balancing the presets. Main's **−1 dBFS safety ceiling** leaves roughly
7 dB above that reference for stronger expression. Fresh focused source
renders pass all 504 wrapper cases at 44.1, 48 and 96 kHz. This record covers
source calibration; SDK compilation, installation and Recon testing are
recorded separately by the Rack port.

## Authored balance

The 20 factory presets store fixed VCA values. Nineteen change only Volume
among their sound parameters. Equipentatonic Log Keys also changes Strike
Position from `0.3` to `0.4` and Resonator Coupling from `0.1` to `0.2`.
Its quiet phrase crest falls from 24.971481 to 22.010946 dB, a **2.960535 dB
improvement before VCA**. This changes modal excitation and register balance;
it is not 2.961 dB attenuation of each individual attack. At matched phrase
peak, its 20–150 ms ringing increases by about 5.73 dB at MIDI 60 and 6.19 dB
at MIDI 84. This is measured body/crest evidence, not a listening assessment.

The physical engine, fixed model recording gains, Main Volume law and musical
Limit behavior remain unchanged. Volume retains its 85% unity reference,
2.25× maximum gain and 5 ms smoothing. Only Main's existing safety ceiling
changes, from −7 to −1 dBFS; linked stereo gain, 2 ms look-ahead, detector,
recovery and latency are retained. The independent direct-output protector
keeps its 0.94 ceiling and native 1× remains the default quality setting.
Saved parameter values are not migrated. Existing user patches can produce
higher accents than under the former −7 dBFS ceiling; this is the intended
restoration of dynamic headroom.

Equal RMS is no longer a goal. Measured reference stereo RMS spans
**−30.096 to −21.859 dBFS**, an approximately 8.24 dB natural difference.
Similar phrase peaks therefore retain different attack-to-body ratios,
sustain and breathing. There is no runtime RMS matcher, cross-preset
normalizer or audio-file normalization.

Two authored exceptions retain their character at maximum VCA:

- **Soft Yarn Nocturne:** −8.617 to −8.521 dBFS across the three rates, a
  small soft offset from the nominal target.
- **Kalimba Thumb Lullaby:** −12.583 to −12.321 dBFS, a deliberate quiet
  exception. Both VCA and Close are already at maximum. Raising it further
  would require changing its soft voicing or the global gain range.

## Complete before/after bank

All entries are actual Rack-wrapper measurements at **48 kHz**, native 1×,
Linear velocity 80, using the same five-second phrase. Before is the 1.5.3
bank with its −7 dBFS safety ceiling; final is the authored 1.5.4 bank with
−1 dBFS safety headroom. Every final phrase needs zero musical and Main guard
reduction. The [CSV](preset-vca-balance-2026-10-20.csv) also retains the final
three-rate peak bounds and former Main gain reduction for each preset.

| Preset | VCA before → final | Main peak before → final (dBFS) | Stereo RMS before → final (dBFS) |
| --- | ---: | ---: | ---: |
| Ambient Cathedral Bloom | 0.92202852 → 0.97358644 | -10.831 → -7.994 | -26.982 → -24.145 |
| Concert Grand Rosewood | 0.88710125 → 0.96615160 | -11.755 → -8.000 | -27.000 → -23.244 |
| Direct Piezo Mallet | 0.82629591 → 0.72057581 | -7.005 → -7.778 | -27.006 → -29.298 |
| Hard Acrylic Virtuoso | 0.83384941 → 0.70143330 | -7.000 → -7.997 | -27.003 → -29.878 |
| Rosewood Dampened Etude | 0.89564379 → 0.97453463 | -11.985 → -8.000 | -26.993 → -23.008 |
| Rosewood Roll Chorale | 0.92118735 → 0.98390567 | -11.486 → -7.929 | -27.001 → -23.444 |
| Soft Yarn Nocturne | 0.94332347 → 1.00000000 | -12.121 → -8.521 | -26.960 → -23.360 |
| Chiapas Folk Fiesta | 0.78840589 → 0.65041673 | -7.012 → -8.000 | -27.012 → -30.088 |
| Guatemalan Charleo | 0.74482374 → 0.75214517 | -8.166 → -7.996 | -27.011 → -26.841 |
| Mayan Dance Padauk | 0.78962565 → 0.91508961 | -11.325 → -7.969 | -26.997 → -23.640 |
| Ancestral Balafon | 0.76980639 → 0.74396729 | -7.406 → -7.999 | -26.995 → -27.588 |
| Birifor Gyil Buzz | 0.76152086 → 0.74793708 | -7.687 → -8.000 | -26.998 → -27.311 |
| Chopi Timbila Ensemble | 0.78796281 → 0.71368384 | -7.000 → -7.984 | -26.993 → -28.632 |
| Equipentatonic Log Keys | 0.91707000 → 0.76532996 | -7.000 → -8.000 | -26.787 → -30.011 |
| Mandinka Bala Heptatonic | 0.79912895 → 0.72506344 | -7.006 → -7.993 | -26.991 → -28.598 |
| Midnight Village Ritual | 0.76918456 → 0.87777889 | -10.439 → -8.000 | -26.995 → -24.556 |
| Sambla Talking Balafon | 0.80419884 → 0.72646320 | -7.020 → -8.000 | -26.989 → -28.666 |
| Kalimba Muted Thumbs | 0.88192243 → 0.98134434 | -12.889 → -7.989 | -27.085 → -22.185 |
| Kalimba Pick Sparkle | 0.82833062 → 0.96537077 | -13.144 → -7.988 | -27.124 → -21.967 |
| Kalimba Thumb Lullaby | 0.99067363 → 1.00000000 | -13.172 → -12.500 | -27.171 → -26.499 |

## Measurement protocol

The primary phrase uses MIDI notes `60, 64, 67, 72, 67, 64, 60, 60, 62, 65,
69, 74, 69, 65, 62, 62, 48, 55, 36, 84`. It has 20 onsets 150 ms apart,
100 ms holds, fixed velocity 80 and Linear response. Each fresh wrapper
instance runs its actual factory patch and all seven output channels. The
five-second window includes the remaining tail and is rounded up to complete
64-frame host blocks: 220,544 frames at 44.1 kHz, 240,000 at 48 kHz and
480,000 at 96 kHz. Stereo RMS is
`sqrt(sum(L² + R²) / (2 × frames))`; peak is the largest absolute Main
sample across both channels. These are sample peaks, not a certified
true-peak mastering specification.

The source probe compiles the actual CMaremba wrapper against the verified
Jukebox SDK **500_028 / TargetVersion 5.0** headers. Passive per-sample
callbacks are added only in a scratch source copy; production DSP is not
rewritten. Independent rendered-buffer energy verifies that each recorded
gain belongs to the delayed sample it actually processes. The VCA solver
replays quiet captures through production OutputHeadroom and inverts the
existing Volume law; the actual XML is then rendered independently.

Quiet captures already contain the 96-frame / 2 ms guard delay at 48 kHz.
Offline replay adds another 96-frame delay and loses 96 extra tail frames
from the same finite window. Reduction and energy loss compare against the
float-rounded, equally delayed input, so the tail truncation is not counted
as compression loss. Final primary reference levels come from the direct
wrapper render, not replay estimates. The isolated-register replay remains
a diagnostic with that tail-window limitation.

## Focused source validation

The [primary reference](preset-vca-balance-reference-2026-10-20.csv) contains
**60 cases**: all 20 presets at 44.1, 48 and 96 kHz. All have zero musical
limiter and Main output-guard reduction. The 18 ordinary presets peak between
**−8.000001 and −7.620924 dBFS**; the two softer exceptions are explicit
above. The natural RMS span is measured, not an equality target.

The broader [wrapper matrix](preset-vca-balance-wrapper-2026-10-20.csv) contains
**504 cases**: 20 presets plus motherboard init, eight scenarios and three
rates. All three strict Main −1 dBFS ceiling gates and direct-output safety
gates pass. No case reports a nonfinite sample or safety reset. The 441 cases
in its normal group include variable-velocity phrases, the fixed reference,
velocity-64/100 single notes, velocity-100 low/middle chords and a velocity-80
wheel roll. Musical Limit contributes **zero gain reduction** throughout.
Only 16 of these 441 fixtures engage Main protection by more than 0.01 dB;
this is a test-case count, not an estimate of how often musical playing limits.

The worst normal-group fixture is Rosewood Dampened Etude's middle chord at
velocity 100 and 44.1 kHz: **2.736 dB maximum reduction**, **1.574 dB energy
loss**, and **87.392 ms** continuously above 1 dB reduction. These measurements
fit the intended normal-play review bounds of 3 dB maximum reduction, 2 dB
energy loss and 100 ms above 1 dB. The 63 velocity-127 dense-chord stress cases
are measured separately; they can need up to **9.668 dB** reduction and do
not carry a gentle-limiting claim.

The [isolated-register replay](preset-vca-balance-register-2026-10-20.csv)
contains **120 cases**: MIDI 36, 48, 60, 72, 84 and 96 for each preset,
velocity 80, 48 kHz, 100 ms holds and five-second windows. All 120 now need
**zero gain reduction** and no compression energy loss beyond floating-point
rounding (under 5×10⁻¹² dB). Their largest sample peak is **−3.548 dBFS**,
Equipentatonic at MIDI 96 (C7 when MIDI 60 is C4). This natural high-note
accent is allowed above the shared phrase reference and below the safety
ceiling. It is no longer flattened to the old −7 dBFS guard.

## Why the safety ceiling changed

The preserved [pre-headroom wrapper matrix](preset-vca-balance-pre-headroom-wrapper-2026-10-20.csv)
shows why the earlier −7 dBFS ceiling could not provide the desired breathing
at the new VCA positions. The same velocity-100 middle chord needed 8.736 dB
reduction and 4.446 dB energy loss, despite the primary phrase needing none.
That prompted more headroom rather than stronger compression or lower VCA
positions on every preset.

In the [earlier register replay](preset-vca-balance-pre-headroom-register-2026-10-20.csv),
Equipentatonic C6 needed 1.129 dB reduction for 38.625 ms above 1 dB, and C7
needed 3.475 dB, 104.229 ms above 1 dB and 2.895 dB energy loss. **Those
compression findings are resolved by the new headroom:** both now pass at
their natural levels with zero reduction. The other 118 register cases are
unchanged. The old threshold review is historical evidence, not a current
failure. Quiet audio hashes and the final factory table are unchanged between
the two register replays.

The [current evidence JSON](preset-vca-balance-evidence-2026-10-20.json)
retains strict ceiling gates, normal reduction metrics, register results,
VCA/reference bounds and raw-evidence hashes. The
[pre-headroom record](preset-vca-balance-pre-headroom-evidence-2026-10-20.json)
keeps the earlier protection scope and Equipentatonic voicing investigation.
Neither is a host playback or realtime timing qualification.

## Evidence scope

The factory bank and Main headroom are authored and the focused source
measurements above pass. The complete preflight, SDK local45 build and
development installation are recorded separately in the Rack port; these
source measurements do not establish Recon playback or host CPU. Historical
1.5.3 and 1.5.2 installation records describe their own artifacts.
