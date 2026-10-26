# Output calibration and overload protection

## 1.5.6d1 louder RMS bank — source validation complete, SDK delivery pending

The user’s practical reference is Concert Grand Rosewood at Reason C3 (MIDI 60),
MIDI velocity 100 and unit Volume 100/127. Preserve the installed 1.5.5 fixed
model gains, the existing Volume curve/2.25 maximum and −1 dBFS Main ceiling.
Balance all twenty factory VCAs around a −22.2 dBFS single/pair maximum-window
RMS comparison, with one declared −22.6 dBFS Direct Piezo reference to preserve
its sharp attack. Concert defaults to 0.78734778, effectively 100/127.

Fresh 126-case reference captures pass at 44.1, 48 and 96 kHz. Bank score spread
is 0.638 dB; every declared target is within ±0.35 dB. Musical/stem reduction is
zero, and only five cases need Main protection above 0.01 dB. Maxima are 2.172 dB
Main reduction, 1.928 dB energy loss and 74.69 ms continuously over 1 dB.
The expanded playing and maximum-velocity suites retain hard output gates and
report deeper protection honestly. This comparison is separate from Reason VU.

Kalimba tine and neighbour-brush lifetimes are 30% shorter. Native gated
calibration passes 216 renders with fresh Init/factory fingerprints; original
numeric safety, tolerance, replay and lifecycle gates remain. A complete fresh
nine-stage SDK build and protected Recon delivery remain pending.
See [current loud-bank evidence](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/loud-rms-bank-2026-10-21/README.md).
The following sections retain historical version-specific measurements.

## 1.5.4d1 VCA balance and dynamic headroom — local45 Deployment built and installed

Normal playing now targets approximately **−8 dBFS Main phrase peaks**,
using factory **Volume (VCA)** as the main balance control. On the fixed
velocity-80 Linear reference at 44.1/48/96 kHz, 18 ordinary presets measure
−8.000001 to −7.620924 dBFS. Soft Yarn Nocturne reaches −8.617 to −8.521
dBFS at full VCA; Kalimba Thumb Lullaby remains a deliberately quiet
−12.583 to −12.321 dBFS exception, with VCA and Close already maximum.

Nineteen presets change only VCA among their sound controls. Equipentatonic
Log Keys also changes Strike Position from 0.3 to 0.4 and Coupling from 0.1
to 0.2, improving the quiet phrase's natural crest by 2.961 dB before VCA.
The fixed model gains, physical engine, Volume law, musical Limit and native
1x default remain unchanged. RMS is allowed to follow each sound's natural
crest and sustain: the reference spans −30.096 to −21.859 dBFS. No runtime
RMS matching or cross-preset normalization is added.

Main's existing smooth 2 ms look-ahead protection now uses a **−1 dBFS
sample-peak safety ceiling**, leaving about 7 dB above the normal reference
for stronger expression. Its gain linking, detector, recovery and latency
are retained. Direct Close, Room and Piezo keep independent protection at
0.94 and continue to bypass Main Volume and processing.

Fresh focused source renders pass **504 wrapper cases** at the three rates,
including strict Main ceiling checks. All **60 factory reference phrases**
need zero musical or Main guard reduction. Across the broader **441 normal
fixtures**, musical reduction stays zero and Main protection reaches at most
2.736 dB reduction, 1.574 dB energy loss and 87.392 ms continuously above
1 dB reduction. The 63 dense velocity-127 stress cases can limit more strongly
and do not carry a gentle-limiting claim.

All **120 offline isolated-register capture replays** now need zero gain reduction; their
largest peak is the natural Equipentatonic C7 accent at −3.548 dBFS. The
earlier −7 dBFS ceiling had compressed it by 3.475 dB; that finding is
resolved by the new headroom rather than hidden by a lower VCA setting.
The [canonical report](../validation/preset-vca-balance-2026-10-20.md)
contains the full bank, protocols, measured exceptions and preserved earlier
candidate evidence. The 504 cases are fresh source-wrapper renders; the 120
register cases replay frozen quiet captures through production protection.

All nine mandatory preflight stages and the SDK 5 local45 Deployment build
pass. The final maximum-velocity stage covers **4,053 wrapper cases**: 2,751
at 48 kHz and 651 each at 44.1/96 kHz. All **231 retained clips** pass 8x
reconstruction checks; reconstructed maxima are 0.893205 for Main and
0.940748 across all outputs. Those reconstructed values are distinct from
the declared sample-peak ceilings.

The **55-file installed payload** is verified for fresh Recon user testing.
Three paired native SDK callback runs use 21.525% less median thread CPU at
1x than 2x. Across 12,288 measured blocks per mode, 1x records 400 wall-time
and 225 thread-CPU deadline misses; 2x records 413 and 285. These are
simulated callbacks, not a timing pass or hosted CPU qualification. Recon
was not launched. See the [build and installation record](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/build-1.5.4d1-2026-10-20/README.md)
for artifact identity, verification and the disclosed loss of the original
full stdout log.

## 1.5.3d1 native-rate default — local45 Deployment built and installed

Native 1x is now the initialized-device and factory Quality setting. The model
gains, Volume law and protection described below are retained. Checking the
same reference phrase at 44.1, 48 and 96 kHz requires one new factory Volume
trim: Equipentatonic Log Keys changes from 0.92831682 to 0.91707000. Through the
Rack wrapper it measures −26.7661, −26.7870 and −27.2320 dBFS stereo RMS at
those rates, within the unchanged −27 ±0.35 dBFS target. Its Main phrase peaks
remain below −7 dBFS. The other 19 factory Volume settings are retained.
See the [native-rate measurements](../validation/native-quality-2026-10-20.md)
and [build and installation record](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/build-1.5.3d1-2026-10-20/README.md).
All nine validation stages pass, including 4,095 maximum-velocity wrapper cases
and 280 retained clips checked with 8x reconstruction. The SDK 5 local45
Deployment payload is installed with all 55 files verified; Recon playback
and host CPU remain unqualified. The 1.5.2d1 measurements and build evidence
below retain their original 2x scope.

## 1.5.2d1 RMS calibration and Main ceiling — local45 Deployment built and installed

All 20 factory presets and the four initialized model states use a common
**−27 dBFS stereo RMS** reference through the real Rack wrapper: a five-second,
20-note phrase across MIDI 36–84 at fixed velocity 80 with **Linear** response.
RMS includes both Main channels and the phrase's tails. This is a repeatable
reference workload, not a promise that every note, register or musical pattern
has the same RMS or perceived loudness.

Four fixed recording gains in `Instrument/Source/DSP/MainCalibration.h`
calibrate Rosewood, Padauk, Balafon and Kalimba respectively at
1.225115902× / 1.174502045× / 1.406057112× / 1.034682015×. The existing 1.202853×
Main calibration and 0.7 recording trim remain. These gains follow Drive and
Warmth alongside Volume, before both dynamics stages, and smooth over 5 ms on
model changes. They never follow instantaneous level or velocity. Factory
Volume positions are calibrated separately; every other physical and capture
parameter, including mallets, tuning, decay, velocity-route depths, Drive and
musical Limit, is retained.

Volume retains its **85% nominal reference** and squared response below that
point. The smooth upper region now reaches **2.25× (+7.04 dB)** at full travel,
reduced from the previous 4× (+12.04 dB). Gain changes retain 5 ms smoothing.
Linear replaces Sigmoid as the initialized and factory-preset default. Older
user patches retain their stored Volume and velocity-curve choices; the new
fixed gains, upper Volume law and Main ceiling intentionally change their
audible level without migrating those values.

The existing smooth lookahead protector now sets **Main's sample-peak ceiling
to −7 dBFS (0.446683592)**. Its 2 ms lookahead, linked stereo gain, detector,
recovery behavior and declared latency are unchanged. Some normal phrases
peak below −7 dBFS; the ceiling reserves headroom for louder velocity,
low-register playing and raised Volume. Protection applies smooth gain when those
signals exceed it, including with musical Limit bypassed. Matching the RMS
reference does not force every normal attack to reach the ceiling.

Direct Close, Room and Piezo retain their independent fixed capture trims and
**0.94** ceiling. They bypass Main Volume, Drive, Warmth, Main calibration and
musical Limit, and keep the same 2 ms protection delay as Main. No Main gain
change enters those stems.

This intentional **1.5.2d1** calibration has an SDK 5 **local45 Deployment**
build installed for fresh Recon user testing, with all 55 installed files
verified. The original build45 preflight timed out; all nine compact gates
were then completed and separate SDK compilation passed against unchanged
Rack `b467140` and canonical `1b771cc` inputs. Native SDK audio checks passed;
the simulated timing workload missed callback deadlines for both this build
and the prior f7 module. This does not establish Recon playback or host CPU
qualification, and no new universal45 package or publication is claimed. See
the [build and installation record](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/build-1.5.2d1-2026-10-20/README.md).
Earlier measurements below describe their named historical source and artifacts
only.

The canonical [measurement protocol and reduction evidence](../validation/output-calibration-2026-10-20.md)
records one exception to light normal-play reduction: Equipentatonic Log Keys
receives 7.525 dB peak reduction and 2.401 dB energy loss on the reference.
The other presets need at most 2.401 dB peak reduction and 0.274 dB energy loss.

## Historical 1.5.1 phrase-peak calibration

The following records the preceding calibration; its phrase-peak target and
Volume endpoint do not describe 1.5.2d1. The old measurements are preserved
without reinterpretation.

All 20 factory presets and all four initialized model states are calibrated
through the real Rack wrapper. The reference is the louder sample peak of two
five-second phrases across MIDI 36–84: fixed velocity 80, and varied velocity
60–119. At 48 kHz the references span −7.176 to −7.000 dBFS; Equipentatonic retains a
small cross-rate centering trim, and the other 23 states peak at −7.000 dBFS. A ±0.35 dB cross-rate
contract covers 44.1, 48 and 96 kHz. This is a phrase-peak reference, not RMS,
LUFS, a constant note level, or a −7 dBFS output ceiling.

`DSP/MainCalibration.h` holds four fixed Main recording gains. The existing
1.202853× calibration and 0.7 trim remain; the new model gain follows Drive and
Warmth, alongside Volume, before both dynamics stages. Model changes smooth
this gain over 5 ms. These constants never follow signal level or velocity.
Main Volume retains its existing law: 85% is unity relative to the fixed model
calibration, and full travel provides 12.04 dB of additional gain.

Only Volume changes in each factory patch. All capture coefficients, mallets,
physical voicing, tuning, decay, velocity routes, Drive and Limit settings are
preserved. Concert Grand Rosewood now stores Volume **0.75954263** and its
varied-velocity phrase peaks at −7 dBFS (previously −9.788 dBFS). Its velocity-80
phrase peaks at −10.373 dBFS; the natural velocity response remains audible.
The same phrase does not give every sound the same RMS or perceived loudness.

The initialized Rosewood/Padauk/Balafon/Kalimba recording gains are respectively
1.726411287× / 1.638942302× / 1.816753899× / 1.414657906×, applied in addition to
the existing Main calibration. Saved custom patches retain their controls and
therefore become about 4.743 / 4.291 / 5.186 / 3.013 dB louder for those models
before protection. Factory Volumes compensate separately. This is an intentional
level change requested for the unit, not a migration of user-saved patches.

Raw Close, Room and Piezo outputs retain their independent fixed recording
trims and bypass all Main gain changes. Set their mixer-channel gains for a
stem mix. Hard chords, different registers, increased Volume, or changed capture
blends can exceed the reference peak and engage the existing 0.94 protection.
Ordinary calibration uses clean Drive and bypassed musical Limit; no compressor,
AGC or lower ceiling is added to force all playing to −7 dBFS.

[Calibration measurements, reproduction and validation](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/peak-calibration-2026-10-16/README.md)
describe the source change. The previously built and installed f7 artifacts
remain unchanged; this source calibration requires a new binary build.

## Previous built f5–f7 mix behavior

F6 retains f5's DSP, controls, defaults and every stored factory parameter
value. Its versioned artwork and documentation refresh introduces no new gain
compensation or loudness matching. The measured scope remains the preserved
[f5 qualification](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/production-2026-10-16-retry1/README.md); the
[f6 build record](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/build-1.5.1f6/README.md) tracks fresh qualification
separately. No new f6 audio or CPU measurement is implied by this source match.

The accepted 1.5.1f3 source voicing is retained. Acoustic-heavy Rosewood presets
are quieter than the earlier matched bank; Padauk and Balafon also retain their
reduced resonator/frame contribution. Kalimba keeps the cleaner bridge/pickup
response. Factory capture controls and Volume have not been normalized again.
The 1.5.0f5 loudness figures below describe that historical source only, and do
not describe the current bank. See the
[source-voicing measurements](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/source-voicing-2026-10-15/README.md) and
[preserved f5 production audit](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/production-2026-10-16/README.md).

For a final mix, set Main Volume against the actual phrase and leave room on
the destination bus for other tracks. The direct Close, Room and Piezo stems
bypass Main Volume, Drive, Warmth and musical Limit; use their mixer-channel
faders to set recording and bus levels. Avoid unintentionally summing Main with
all three direct captures, since Main already blends those sources.
Check the combined mix in mono, especially when bringing up the spatial Room
stem or combining Piezo with the microphones. Invert Piezo changes the acoustic
blend, so compare it at a matched level. Output protection bounds overloads;
it does not replace gain staging in the song's final mix.

## 1.5.0f5 visible mixer balance

The factory bank now uses the visible capture controls together with Volume,
while retaining the f4 Volume law and fixed Main/direct calibration below.
Fourteen presets use 85–90% Volume, including five at 85%; the gentle Lullaby
retains 99.21%. No saturation or wet gain is added solely to force an outlier's
Volume position. Drive remains linear at its 10% default and musical Limit
remains bypassed through 30%, with brief independent output protection allowed.

The proportional capture/Volume adjustment preserves the default linear Main
blend. Since the capture controls precede Drive, raising Drive later can produce
a different degree of saturation. Direct stems remain independent, with fixed
recording trims for individual mixer channels. With the final Room, the factory
bank spans 0.202 LU on the common velocity-80 phrase (−18.147 to −17.945 LUFS)
and 2.331 LU with varied velocity. These are repeatable internal comparisons;
they do not establish the level of an arbitrary other instrument or pattern.
The separate [three-rate output-protection evidence](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/preset-loudness-2026-10-08/f5-final-bank/README.md)
records the final bank's headroom; see the device README for completion status.
The f4 measurements below retain their original source scope.

## Retained 1.5.0f4 reference level and preset matching

Volume now places the normal level reference at 85%. Below that point its
amplitude is `(Volume / .85)^2`. Above it, a continuously differentiable boost
region reaches 4x amplitude (+12.04 dB relative to 85%) at full travel. The Main
calibration is 1.202853x with the existing 0.7 recording trim. At 50%, level is
9.22 dB below the reference; at 95%, it is 6.03 dB above it. Existing 5 ms amplitude
smoothing is retained. This gain follows Drive and precedes both dynamics
stages; it never reaches any direct output or changes the Drive input.

The factory bank measures -18.155 to -18.000 LUFS on the common velocity-80 phrase,
about 1.09 dB below the previous Concert reference. This is an internal,
repeatable comparison, not a claim that every other instrument or musical
pattern has that loudness. The varied-velocity phrase spans 2.33 LU because the natural velocity responses
are preserved. Saved factory Volume spans 78.74–99.21%, with Concert at exactly
85%; this is a wider spread than a few percentage points around the reference.
Only saved Volume values change in the bank;
instrument, capture, striker, tuning and decay settings retain their identity.

**Policy selected on 8 October:** retain current loudness and the hard attacks,
allowing brief independent output protection even below 30% Limit. The musical
Limit stage itself still applies no reduction or makeup through 30%. The earlier
f3 requirement of zero output-guard action on all ordinary playing is retained
as historical evidence, not claimed for the louder matched f4 bank. New duration
measurements distinguish short attack protection from sustained attenuation.
The full sweep passes 4,284 ordinary cases across 44.1, 48 and 96 kHz: musical reduction is zero, isolated protection recovers below 1 dB within 200 ms, and outputs remain protected. Main reduction reaches 8.13 dB on the hardest attacks; Room direct protection reaches 0.52 dB, while Close and Piezo guards remain inactive. Equipentatonic rolls renew protection across several strokes, with continuous episodes up to 255 ms and about 1.1 dB integrated energy loss; these are explicitly reviewed as repeated transient control, not isolated attacks or inaudible processing. See the [calibration evidence](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/preset-loudness-2026-10-08/README.md); current SDK and release status is in the main README.

Existing songs and custom patches keep their stored controls but follow this
new curve. Unchanged Volume settings through 85% are 4.52 dB quieter than f3;
full Volume is 4.70 dB louder. Volume CV and automation follow the new curve.
Factory values are recalibrated; user-saved values are not migrated.

## Retained 1.5.0f3 Main Drive calibration

Drive now follows the complete Close/Room/Piezo Main mix. All five direct
captures bypass Drive. Bare init and all 20 factory patches store Drive 0.10;
Limit remains at 0.05 or below; Equipentatonic Log Keys retains 0.00. The Drive transfer is exactly linear through 10%, with
`1 + 0.12 × Drive` gain in that region: 1.012, about +0.10 dB, at the default.
Above 10%, increased input gain reaches a rounded, mildly asymmetric soft
saturation curve with partial output-level compensation. First-order
antiderivative antialiasing reduces generated aliases without eliminating them.
Live Drive changes follow a 10 ms time constant; idle and reset settings apply
immediately. The linear default has no ADAA delay, while the active stage has
a frequency-dependent phase response.
Warmth, squared Volume, the 2.8× Main calibration and 0.7 trim follow Drive;
output gain changes do not increase the input to the Drive stage.

This changes older songs at their saved Drive positions and removes Drive from
direct Piezo. Main delays each pickup contribution by its source-to-Close-head
acoustic flight time, retaining its original electrode polarity. The direct
Piezo retains the electrical signal's original timing and response.

The wooden instruments' fixed direct Piezo trim is **0.245**, down from 0.49
in f2, providing another **6.02 dB** of recording headroom after removing the
old direct-path saturation. The shared 0.65 capture scale still follows this
trim. Close and Room stay at 0.28 and 0.385; Kalimba's Close, Room and Piezo
remain 0.56, 0.546 and 0.581. These trims do not enter Main.
**Direct Piezo Mallet** now stores Volume **0.53** (previously 0.70799348);
that change affects Main only. Seven other patches receive static Volume trims
from the final-source ordinary-playing sweep, targeting another 0.5 dB below
their measured overload boundary. Concert Grand Rosewood moves from 0.75063051
to **0.69770352**; its Volume change alone is **−1.27 dB**. That is a control-gain
change, not a measurement of the complete f3 phrase: Drive and capture alignment
also affect its level. The other 12 factory Volume values remain at f2 settings.
The final verification sweeps pass all **4,284 ordinary cases at 44.1, 48 and
96 kHz** across the bank and bare init, with musical limiting and all four
output guards inactive. At 48 kHz, Concert's varied-velocity phrase measures
**−19.819058 dBFS RMS**, compared with **−19.281293 dBFS** in f2:
**0.537765 dB quieter**. The complete sweeps contain 4,347 cases; their 63 dense
maximum-velocity stress cases remain separately reported and can engage
protection. This scope does not guarantee clean output for arbitrary user settings.
Both SDK builds and guarded Recon creation/conversion passed for f3; see
[its retained qualification](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/build-1.5.0f3/README.md).

| Factory patch | f2 Volume | f3 Volume |
| --- | ---: | ---: |
| Chiapas Folk Fiesta | 0.70190167 | 0.59371757 |
| Concert Grand Rosewood | 0.75063051 | 0.69770352 |
| Direct Piezo Mallet | 0.70799348 | 0.53 |
| Guatemalan Charleo | 0.76670739 | 0.65286329 |
| Hard Acrylic Virtuoso | 0.71008952 | 0.61852254 |
| Mayan Dance Padauk | 0.88 | 0.74865512 |
| Rosewood Dampened Etude | 0.75328325 | 0.69849992 |
| Soft Yarn Nocturne | 0.78536472 | 0.75139026 |

The [seven-patch measurement map](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/drive-captures-2026-10-08/factory-level-adjustments.json)
records the observed guard deficits and additional 0.5 dB margin. Direct Piezo
Mallet's earlier adjustment precedes that sweep. These are authored factory
settings; user-saved Volume positions are not migrated.

The final f3 ordinary-playing level and headroom scope is measured above;
those strict zero-reduction results apply to f3's saved bank only.
The retained measurements below apply to f2 and earlier named versions.

## Validated 1.5.0f2 calibration, 7 October 2026

Natural Decay is at 50%, the factory default no longer adds Decay with velocity,
and Room uses a squared control taper with shorter, lighter tails. A bar shaft
stroke has 50% of the head-strike speed and 40% of its former extra click level;
these are fixed voicing choices, not audio-dependent gain reduction.
Wooden-bar upper-mode and transverse radiation contributions use fixed 0.08
and 0.15 weights, reducing exaggerated attack energy at its source.

Main uses a selected 2.8× calibration and 0.7 trim **before** its
musical limiter, together with smoothed squared Volume. Lowering Volume now
reduces detector drive. Direct capture trims and the final four independent
protection stages remain unchanged.

The first 30% of **Limit** applies no musical gain reduction or makeup. Above
that, engagement rises smoothly through 37% and the remaining travel reaches
the prior maximum effect. This knob range is distinct from the independent
overload protection on all seven outputs.

Bare init uses **Volume 0.68**. Sixteen factory patches receive individual
Volume reductions; four retain their prior values. The calibration uses the
worst Main interpolation-detector peak among **68 normal cases per patch at
48 kHz**: seven musical patterns plus every MIDI key from 36–96 at velocity
100. It targets **−0.69 dBFS**, about 0.15 dB below the 0.94 ceiling.
The squared Volume law gives
`newVolume = oldVolume * 10^((targetDb - worstDetectorDb) / 40)` for patches
above that target. The [authored map and measurements](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/air-headroom-2026-10-07/factory-volume-calibration.json)
record the exact values and witness cases. Seven patches needed further trims
after the chromatic range sweep; the prior musical-pattern map is retained
separately. Stress cases do not set these trims.

**Concert Grand Rosewood** uses Volume **0.75063051**, independently of bare
init. Its varied-velocity phrase measures about **−19.28 dBFS RMS**,
**3.28 dB below 1.5.0f1**; the Volume adjustment alone is −1.11 dB. This accepts
a modestly quieter default to preserve the attack. The final 48 kHz sweep
covers 1,449 cases across the 20 factory patches and bare init. All **1,428
normal cases** have zero measured musical and output-protection gain reduction;
the 21 stress cases remain separately reported. Both SDK builds and the limited
Recon creation/conversion checks passed; see the [f2 build evidence](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/build-1.5.0f2/README.md).
These settings do not promise clean output
for every user patch, control combination, MIDI register or stacked chord;
independent protection remains available for overloads.
See [the measurements and unnormalized listening files](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/air-headroom-2026-10-07/README.md).
The historical validations below apply only to their named builds.

Version **1.5.0f1** retains the 1.4.0f7 DSP and factory parameters unchanged.
The measurements and comparisons below keep their original version labels.

## 1.4.0f7: 70% output calibration

The selected sound follows listening option **02**, reduced from its 80%
calibration to **70%** (another −1.16 dB). Main keeps the original 4× gain before
its musical limiter, then applies **0.7 after that limiter and before output
protection**. Every direct capture's fixed trim is 70% of f6. Init and all 20
factory patches cap **Drive and Limit at 0.05**, retaining their original Close,
Room and Piezo coefficients, relative capture balance, Volume and physical
parameters.

| Fixed stage | f6 | f7 |
| --- | ---: | ---: |
| Main pre-Limit gain | 4 | 4 |
| Main post-Limit calibration | 1 | 0.7 |
| Bar Close / Room / Piezo trims | 0.40 / 0.55 / 0.70 | 0.28 / 0.385 / 0.49 |
| Kalimba Close / Room / Piezo trims | 0.80 / 0.78 / 0.83 | 0.56 / 0.546 / 0.581 |

Direct trims also retain the common 0.65 capture scale. **70% is a gain-stage
ratio**, not a claim that every preset's measured RMS is exactly 70% of f6:
Drive/Limit caps, reverb improvements and active overload protection also affect
the result. The selected policy permits internal gain reduction. The rejected
full-clean candidate made the default roughly 18–20 dB quieter and is retained
only as comparison evidence in
[the earlier calibration investigation](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/normal-playing-level-2026-10-06/README.md).

### Protection and verification

Main L/R, Close L/R, Room L/R and mono Piezo have **four independent output
limiters**. Stereo pairs share gain; a hot Close signal cannot attenuate Room
or Piezo. Their 0.94 sample ceiling (−0.54 dBFS), 2 ms lookahead, interpolation
detector and latency are unchanged. The musical Limit control still shapes
Main only. Attenuation is applied before output protection, so even maximum
velocity, hot keys and stacked notes remain protected.

Validation checks every factory patch plus bare init at maximum velocity over
the MIDI range, with dense chords and rolls, finite-output/reset checks and
independent reconstructed-peak analysis of the strongest outputs. Internal
gain reduction is measured and reported; it is allowed under the chosen sound.
The safety ceiling is not a universal true-peak mastering guarantee, and an
external mixer can add gain after the device.

The [recorded final sweep](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/max-velocity-2026-10-06/README.md) passes
**4,515 cases**: every MIDI note 0–127 in all 21 states at 48 kHz, plus 39
selected critical keys at both 44.1 and 96 kHz; each rate also includes dense
repeated chords and maximum-wheel rolls. Maximum sample peak is 0.940000057
(float rounding at the 0.94 ceiling). There are no non-finite samples or
emergency resets. All 347 retained seven-channel peak/slope captures pass
independent 8× reconstruction, with maximum 0.942111335, including boundaries.
These are native wrapper measurements; SDK packaging and host qualification
are recorded separately.

The SDK 5 documents reviewed do not prescribe a numeric instrument peak, RMS
or LUFS target. Its acceptance checklist asks for a usable default sound and
factory patches. The supplied screenshot's instantaneous meter reading does
not establish an average loudness target. The 70% calibration and retained
output ceiling are Maremba product decisions.

### Compatibility

Property identities, normalized ranges, automation, physical parameters,
capture balance and Volume's squared response are retained. Factory/init caps
are authored values, not runtime clamps: user patches retain their full Drive
and Limit ranges. Existing saved patches receive the new fixed Main and direct
gains while keeping their stored controls. Direct outputs still bypass Volume,
the capture knobs, Width, Warmth and musical Limit; Piezo retains pickup Drive.

## Historical f4–f6 output changes

The sections below preserve measurements and decisions from earlier revisions;
their louder-level claims and trim values do not describe f7.

### Limit auto-gain and a louder main output

Reason Studios did not accept the **1.4.0f1** submission because its sound was
too low. This update raises Maremba's level in the engine alone. No control,
range, default, MIDI or Remote identity or factory-patch value changes.

**1.4.0f5** then corrected the five direct outputs, which this update left far
below Main: see [Direct outputs: 1.4.0f5](#direct-outputs-140f5) below.

## What changed

| Stage | Before | Now |
| --- | --- | --- |
| **Limit** makeup | none: raising Limit only lowered the level | automatic, `15 * amount^1.4` dB: 0 dB at the bottom of the knob, +2.2 dB at the default 0.25, +15 dB at full |
| Main calibration before Limit | 2× (+6.02 dB) | 4× (+12.04 dB) |
| Direct-output trim (all five stems) | 0.06 (−24.4 dB) | 0.24 (−12.4 dB); per stem from 1.4.0f5 |

- **Limit auto-gain.** Limit lowers the limiter's threshold from −3 to
  −18 dBFS; the makeup raises its output by the same amount. Limited material
  therefore stays at −3 dBFS (before Volume) at every setting, and everything
  under the threshold comes up, so more Limit is louder and denser, never
  quieter. The makeup follows the smoothed threshold (no zipper on
  automation) and the 2 ms engagement crossfade, and starts at 0 dB, so the
  level is continuous with Limit 0, which stays an exact bypass. See
  [the dynamics description](analog-limiter.md).
- **Main calibration.** The lift sits before the limiter, where the 2× did.
  With the makeup holding limited material at its reference, the lift mostly
  raises what sits below the threshold: soft playing and the ringing tails.
- **Direct outputs.** Their fixed −24.4 dB attenuation predates the per-stem
  overload protection and the 27 September source correction, which lowered
  hard-strike Close and Room peaks by 4–14 dB. At the new trim a velocity-127
  eight-note chord (C3 to E5) stays under the stems' 0.94 ceiling on 18 of
  the 20 factory patches; the two with a click far above the ring (Direct
  Piezo Mallet, Equipentatonic Log Keys) take 2.7 and 5.2 dB of stem
  protection on it.
- **Unchanged:** the 0.94 sample-peak ceiling and the 2 ms lookahead
  protection on all seven outputs, physical excitation, velocity response,
  Piezo Drive, Warmth, Width and the squared Volume law.

## Measured levels

The [level probe](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/output-level-2026-09-29/level_probe.cpp) renders
every factory patch through the engine as the wrapper maps it, at 48 kHz.
*Phrase* is `AllPatchRenderTest`'s melodic phrase (20 strikes over four
octaves at velocities 60–119) on Main L/R; *C4 v100* is one strike's first
second. Loudness is BS.1770-4 integrated LUFS. *Guard* is the main output
protection's gain reduction during the phrase in the new build (maximum and
time average), measured with a counter added to a scratch copy of
`OutputHeadroom`.

| Patch | Phrase f1 | f4 | New | C4 v100 f1 | New | Guard max / mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ambient Cathedral Bloom | −22.7 | −16.6 | −9.6 | −28.5 | −14.2 | 1.0 / 0.05 dB |
| Ancestral Balafon | −16.2 | −15.0 | −12.9 | −22.8 | −21.1 | 7.2 / 1.16 dB |
| Birifor Gyil Buzz | −16.1 | −14.8 | −12.6 | −22.4 | −20.4 | 6.9 / 1.14 dB |
| Chiapas Folk Fiesta | −16.9 | −16.2 | −13.4 | −23.1 | −21.0 | 11.0 / 1.42 dB |
| Chopi Timbila Ensemble | −16.4 | −14.9 | −12.7 | −23.1 | −16.3 | 7.3 / 1.06 dB |
| **Concert Grand Rosewood** (default) | **−16.9** | −13.3 | **−10.1** | **−23.6** | **−13.9** | 0.6 / 0.03 dB |
| Direct Piezo Mallet | −18.6 | −15.5 | −12.0 | −26.0 | −18.6 | 11.8 / 2.31 dB |
| Equipentatonic Log Keys | −16.9 | −15.6 | −13.5 | −19.2 | −21.1 | 20.5 / 4.30 dB |
| Guatemalan Charleo | −14.8 | −13.9 | −12.0 | −20.7 | −17.2 | 10.0 / 1.67 dB |
| Hard Acrylic Virtuoso | −16.8 | −16.5 | −12.5 | −24.3 | −17.9 | 10.2 / 1.21 dB |
| Kalimba Muted Thumbs | −20.0 | −15.9 | −11.7 | −27.7 | −18.1 | 1.8 / 0.03 dB |
| Kalimba Pick Sparkle | −16.4 | −15.6 | −10.5 | −23.7 | −13.8 | 0 / 0 dB |
| Kalimba Thumb Lullaby | −23.4 | −17.7 | −10.7 | −26.7 | −17.6 | 0 / 0 dB |
| Mandinka Bala Heptatonic | −16.3 | −14.3 | −12.2 | −22.8 | −20.3 | 5.7 / 0.92 dB |
| Mayan Dance Padauk | −17.1 | −14.9 | −10.1 | −24.1 | −15.7 | 6.1 / 0.53 dB |
| Midnight Village Ritual | −16.7 | −15.3 | −10.5 | −22.9 | −14.2 | 6.2 / 0.66 dB |
| Rosewood Dampened Etude | −18.6 | −15.1 | −10.1 | −24.0 | −13.6 | 2.7 / 0.08 dB |
| Rosewood Roll Chorale | −17.9 | −11.7 | −9.9 | −24.8 | −11.4 | 0 / 0 dB |
| Sambla Talking Balafon | −16.6 | −15.2 | −13.1 | −21.6 | −21.1 | 7.6 / 1.29 dB |
| Soft Yarn Nocturne | −19.6 | −12.9 | −9.9 | −24.8 | −13.4 | 0 / 0 dB |

- The bank's median phrase rises from −16.9 LUFS (f1) to −11.8 LUFS, and its
  spread narrows from 8.6 to 3.9 LU. The quietest phrase was −23.4 LUFS
  (Kalimba Thumb Lullaby); it is now −13.5 (Equipentatonic Log Keys).
- The default patch rises 6.8 LU on the phrase and 9.7 LU on a single note.
  Every main-output peak stays at or below the 0.94 ceiling (−0.54 dBFS).
- On the phrase's RMS, which `AllPatchRenderTest` bounds, the bank measured
  −26.6 to −20.3 dBFS in f1 and measures −18.6 to −11.6 dBFS now, the same
  through the wrapper at 44.1, 48 and 96 kHz.
- The direct outputs rise by 12.04 dB against f4. On the phrase: Close −36.6
  to −23.5 LUFS (f1: −48.8 to −33.3), Room −32.8 to −20.0 (f1: −50.0 to
  −41.3), mono Piezo −38.9 to −28.5 (f1: −55.0 to −47.0).

## Where the protection works harder

The soft-mallet rosewood patches and the kalimbas barely touch the main
protection. The balafon and padauk patches and the two hard-mallet rosewood
ones (Hard Acrylic Virtuoso, Direct Piezo Mallet) now reach it on most
strikes: 6–12 dB at the attack, 0.5–2.3 dB on average over the phrase,
against at most 4 dB at the attack in f4. Their mallets' clicks are shorter
than their limiter's 2–10 ms attack, the makeup lifts them with everything
else, and the lookahead protection holds them at the ceiling; that is
transient limiting, not clipping. Because the click reaches the ceiling first, an isolated C4 of
Ancestral Balafon, Birifor Gyil Buzz, Chiapas Folk Fiesta, Mandinka Bala
Heptatonic or Sambla Talking Balafon gains only 0.5–2.5 LU on f1, while
their phrases gain 3–4 LU. At velocity 127 on an eight-note chord the main
protection reduces 3–19 dB on every patch but Equipentatonic Log Keys.

**Equipentatonic Log Keys** is the outlier. Its wood stick on a carved
balafon key makes a 0.1 ms click about 43 dB above the ring at its playing
position ([round 6](physical-models-round6.md)), and it is the one factory
patch with Limit at 0 and Volume at full. The protection already reduced up to 14.5 dB
on its phrase in f4; it now reduces up to 20.5 dB (4.3 dB on average), and 31
dB on a forte chord. Lowering its Volume or raising its Limit would give its
attack more room; that is a patch edit and not part of this engine update.

## Compatibility

Against the f4 source, existing songs and patches play louder: Main L/R by
6.02 dB plus the Limit makeup of their Limit setting, less what the limiter
and protection take on loud strikes, and the direct outputs by 12.04 dB
(against f1, which had no main calibration, the output stages add 12.04 dB
plus the makeup, on top of the f2–f4 sound changes). Raising Limit now makes
the sound louder, where it used to make it quieter. Lower Volume or the mixer channel where a song needs the old
balance.

## Validation

- The SDK-free steps of `Tests/run_all_tests.sh` pass: the validators,
  `test_dsp`, the touch contracts, all 51 contracts in `Tests/contracts.txt`
  (among them the complete `ExtremeRangeTest` matrix, 12,140 cases and 177
  million output frames with no emergency reset and every channel ≤ 0.94,
  and `EngineFuzzTest`), the headroom reconstruction and the calibration
  tools. Two generator checks were not counted: `generate_kalimba_shapes.py`
  reports its table stale identically on the unchanged source on the Linux
  host used (CI does not run it), and `generate_bar_modes.py --check` was not
  rerun because no bar-mode input changed.
- The tests that rebuild the direct-output path from its published trim or
  the main calibration (`test_dsp`, `CloseEngineArrivalTest`,
  `Round3ResponseTest`, `Round7BalafonKalimbaTest`, `StrikeVarianceTest`,
  `DryCaptureProbe.h`) use the new values. The limiter tests check the makeup
  instead of unity gain: a signal far below the threshold gains exactly
  15 dB at full Limit, limited material settles at −3.00 dBFS, and stereo
  balance holds to 1e-6 relative to the now larger samples.
- `AllPatchRenderTest`'s phrase window moves up with the bank (see that file).
  It and `WrapperHostTest` compile only against the Jukebox SDK, so here they
  were run against a local, clean-room stand-in for `Jukebox.h`; run steps 7–8
  against the real SDK before a build (`build45.py universal45` does).
- Not verified here: listening, Reason or Recon playback, and a cloud build.

## Direct outputs: 1.4.0f5

The raise above left the direct outputs 13–23 LU under Main. On the bank's median
phrase (the same phrase and BS.1770-4 loudness as above) Close measured
−29.5 LUFS, Room −24.9 and Piezo −34.6 (mono, as its dual-mono loudness less
3.01 dB), against −11.8 for Main. Piezo and Room were reported as too quiet.
The [evidence folder](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/direct-output-level-2026-09-29/README.md) holds the
probe and every measurement below.

### Why they were quiet

Found by rendering the factory bank and changing one control at a time on the
default patch. Neither cause is a defect in a capture: no capture, pickup or
microphone stage was found mis-scaled (the Piezo calibration is a fixed
audio-scale reference to the carved C4), and the Level knobs never reached a
direct output (identical at 0 and 1).

1. **Volume reached them.** The direct outputs carried the master Volume's
   squared law, although Volume belongs to Main. Volume 0.5 lowered them 8.1 dB
   and Volume 0 silenced them; at the default 0.80 the law costs 3.9 dB, and
   the factory bank stores 0.78 to 1.00.
2. **One trim, sized for the worst chord, for three different captures.** The
   shared 0.24 trim was chosen so the loudest hard-mallet chord stays under the
   protection ceiling. On the same full-velocity chord the median patch peaked
   at −7.7 dBFS on Close but only −10.2 on Room and −13.4 on Piezo, so the trim
   left 2.5 and 5.7 dB more unused headroom on Room and Piezo than on Close, and
   on ordinary playing (the phrase's median peaks −16.9, −15.2 and −19.9 dBFS)
   all three sat far below Main's −0.5.

### What changed

| Stage | Before | Now |
| --- | --- | --- |
| Volume on the direct outputs | squared law | none |
| Close trim (with the 0.65 mix scale) | 0.24 | 0.40 |
| Room trim | 0.24 | 0.55 |
| Piezo trim | 0.24 | 0.70 |

- **No Volume, no Level knob.** Volume, Close/Room/Piezo Level, Width, Warmth
  and Limit shape Main only. A direct output is the same audio whatever they are,
  to the last bit (`DirectOutputLevelTest`). Volume 0 no longer silences them.
- **A trim per capture.** Each is set so a velocity-127 eight-note chord (C3 to
  E5) reaches the top of its stem's own protection on the median factory patch.
  All three stems then peak together on forte playing, and ordinary playing sits
  well below the ceiling. The constants are `MarembaEngine::kCloseStemTrim`,
  `kRoomStemTrim` and `kPiezoStemTrim`; from 1.4.0f6 the Kalimba has its own
  (`kKalimbaCloseStemTrim`, `kKalimbaRoomStemTrim`, `kKalimbaPiezoStemTrim`).
- **The Room outputs' labels were wrong.** Reason listed them as "Direct + Room
  at ROOM Level"; they never followed the ROOM knob. They now read "Raw Direct +
  Room Return".
- **Unchanged:** physical excitation and velocity response, the captures, Piezo
  Drive, the 0.94 sample-peak ceiling, the 2 ms lookahead protection on all seven
  outputs, and every Main-output value (bit-identical on all 20 factory patches).

### Measured levels

Bank medians over the 20 factory patches, 48 kHz. *Phrase* is
`AllPatchRenderTest`'s melodic phrase; *chord* is a velocity-127 eight-note chord,
C3 to E5. Peaks are sample peaks in dBFS.

| Output | Phrase LUFS before → now | Phrase peak before → now | Chord peak before → now |
| --- | ---: | ---: | ---: |
| Close | −29.5 → −21.5 (+8.0) | −16.9 → −9.9 | −7.7 → −1.4 |
| Room | −24.9 → −15.5 (+9.4) | −15.2 → −6.2 | −10.2 → −0.5 |
| Piezo (mono) | −34.6 → −22.6 (+12.0) | −19.9 → −7.7 | −13.4 → −0.6 |
| Main (unchanged) | −11.8 | −0.5 | −0.5 |

Against Main's median phrase loudness the gap closes from 17.6 to 9.7 LU (Close),
from 13.0 to 3.7 LU (Room) and from 22.7 to 10.8 LU (Piezo, the spikiest signal).
On the default patch a mezzo-forte C4 (velocity 80) peaks at −14.7 (Close), −10.3
(Room) and −14.8 dBFS (Piezo); it peaked at −23.0, −21.4 and −27.9 before.

A direct output rises by its trim ratio (+4.4 dB Close, +7.2 Room, +9.3 Piezo) plus
the squared Volume it no longer carries (+3.9 dB at Volume 0.80, +4.3 at the
bank's lowest, 0.78, none at 1.00).

### Where the protection works

The trims leave ordinary playing untouched. Over the bank's phrase the direct
outputs' protection takes no gain on Close or Piezo and at most 0.8 dB on Room
(three patches, on their loudest strikes); its mean is at most 0.02 dB everywhere.
It works on forte chords, as Main's does: on the loudest patch's velocity-127
chord it reduces up to 9.7 dB on Close (Equipentatonic Log Keys; 5.2 dB before),
7.6 dB on Room (Mandinka Bala Heptatonic; none before) and 0.8 dB on Piezo. That
is transient limiting on the hardest strikes of the hard-mallet and balafon
patches, not clipping.

### The Kalimba: 1.4.0f6

The 1.4.0f5 calibration left the Kalimba's direct outputs behind the bars'. Its
Main was never affected (at the default patch's settings it measures −10.4 LUFS
against the Rosewood's −10.1, because Limit's makeup evens the instruments out),
but the stems carry no makeup and the Kalimba radiates and picks up less, its Close
capture most of all. On the three factory Kalimba patches the median phrase
measured −27.0 LUFS on Close, −19.9 on Room and −25.6 on Piezo, which is 5.7, 5.1
and 3.5 dB under the 17 bar patches' medians (−21.3, −14.8, −22.1), and at the
default patch's settings its Close chord peaked at −7.5 dBFS against −3.0 to −3.5
for the bars. The evidence is in
[direct-output-kalimba-2026-09-30](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/direct-output-kalimba-2026-09-30/README.md).

The Kalimba now has its own trims, the bars' plus 6.0, 3.0 and 1.5 dB:

| Trim (with the 0.65 mix scale) | Close | Room | Piezo |
| --- | ---: | ---: | ---: |
| Bars (Rosewood, Padauk, Balafon) | 0.40 | 0.55 | 0.70 |
| Kalimba | 0.80 | 0.78 | 0.83 |

They bring the Kalimba patches' median phrase to within about 2 dB of the bars':
−20.9 LUFS on Close, −16.8 on Room and −24.2 on Piezo, gaps of +0.4, −2.0 and −2.1 dB
(from −5.7, −5.1 and −3.5). At the default patch's settings the Kalimba's Close
chord peaks at −1.5 dBFS and its mezzo-forte note gains exactly 6.0, 3.0 and
1.5 dB. The protection stays idle on every phrase and takes at most 3.0 dB on Room
and 1.5 dB on Piezo on a velocity-127 chord, none on Close. Kalimba Thumb Lullaby, the
softest patch, remains the softest (−26.2 LUFS on Close).

Main is bit-identical to 1.4.0f5 on all 20 patches, and so are the Close, Room
and Piezo outputs of all 17 bar patches (rendered to WAV and compared byte for
byte; the three Kalimba patches' stems are the only ones that change). The trims
follow Model: exact when the instrument is idle or after a reset, and moved at the
5 ms live-control rate, in either direction, when Model is changed while a note
rings, so switching instruments never steps a ringing note (`DirectOutputLevelTest`).

### Compatibility

Existing songs' direct outputs play louder by the amounts above, and no longer
follow Volume (its automation and CV included). Lower the mixer channel where a
song needs the old balance. Main L/R, every control and every factory patch are
unchanged. A Kalimba song's direct outputs are a further 6.0 (Close), 3.0 (Room)
and 1.5 dB (Piezo) louder in 1.4.0f6 than in 1.4.0f5; the bars' are identical.

### Validation

`Tests/DirectOutputLevelTest.cpp` is new: it fails on the 1.4.0f4 source (a
direct output changes with Volume, and a full-velocity chord leaves Close, Room
and Piezo 5 to 15 dB under their ceiling) and passes now. It renders four models
against Volume, the three Levels, Width, Warmth and Limit and requires the five
direct outputs bit-identical, then requires every stem's chord peak within its
ceiling, near it, and level with the others on every instrument (the Kalimba
failed this until 1.4.0f6). It also holds the model-dependent trims to their
contract: exact at rest and after a reset, ramped in both directions when Model
changes mid-ring, and exact for a bar that sounds; two mutants (a step instead of
a ramp, trims that never follow Model) fail it. The tests that rebuild the direct
path from its published trim (`test_dsp`, `CloseEngineArrivalTest`,
`Round3ResponseTest` and `Round7BalafonKalimbaTest`, which take the Kalimba's
trim for the Kalimba, `StrikeVarianceTest`, `DryCaptureProbe.h`) use the
new per-stem constants and no Volume; the two that need a quiet fixture now get
it from a soft strike, since Volume no longer lowers a stem.
