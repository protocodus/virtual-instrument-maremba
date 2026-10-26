# Main dynamics and clean output headroom

The main bus retains the circuit-inspired detector, control capacitor and
release reservoir behind **Limit**, **Attack** and **Release**. In the developing
1.5.0f3 source, Drive processes the complete Close/Room/Piezo Main mix; all direct
outputs bypass it. The 10% default has an exactly linear Drive transfer with
about +0.10 dB gain. Above 10%, rounded asymmetric soft saturation strengthens
progressively with partial output-level compensation. First-order antiderivative
antialiasing reduces generated aliases; it does not establish alias-free output.
This is an authored preamp response, not a replica of named analog hardware.
Active ADAA has a frequency-dependent phase response; the exactly linear region
has no ADAA delay. No new fixed host latency is declared.
Drive automation follows a 10 ms time constant while audio is active; idle and
reset settings apply immediately. Warmth retains its separate 5 ms smoothing.

Warmth, smoothed squared Volume, 2.8× Main gain and a 0.7 trim follow Drive and
precede musical limiting. Bare-init Volume is 0.68. Direct Piezo Mallet now uses
Volume 0.53, and seven further patches have static trims from the measured
ordinary-playing peaks. Concert uses 0.69770352; 12 patches retain their f2
Volume values. The final sweeps pass all 4,284 ordinary bank/init cases at
44.1, 48 and 96 kHz without musical or output-protection gain reduction;
the 63 dense stress cases remain separate.
Wooden direct Piezo uses a separate 0.245 trim,
6.02 dB below f2, without changing Main or the Kalimba's direct calibration.
A separate 1,428-case ordinary sweep and successful local builds belong to the
retained f2 source. Those earlier receipts do not qualify the f3 SDK build. See [f2 measurements](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/air-headroom-2026-10-07/README.md) and
[build evidence](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/build-1.5.0f2/README.md).

The source correction precedes these dynamics: upper wooden-bar modes use
their signed spatial projection, and the acoustic pressure readout includes
the emitting area relative to the same model's C4 bar. This removes the former
coherent point-source share and the oversized reference area on treble keys.
The impact and membrane-edge noise filters now have unity gain at their
specified centre frequency. Contact mechanics and physical velocity response
remain active. These are relative acoustic calibrations, not measured absolute
SPL; register balance and hard-striker timbre intentionally change. The
[validation report](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/hardest-output-2026-09-27/README.md) separates
these source changes from the final safety stage.

## Controls

- **Limit from 0–30%** bypasses the musical stage and clears its control
  envelope. The 5% default has unity gain, with no makeup or musical reduction.
- **Limit above 30%** uses `active = max(0, (amount - 0.30) / 0.70)` and
  `thresholdDb = -3 - 15 * active^1.4`. The threshold falls from −3 to
  −18 dBFS over the remaining travel. Automatic makeup is `15 * active^1.4`
  dB, following the smoothed threshold. The contribution fades in over
  30–37%: with `x = min(1, 10 * active)`, engagement is `x*x*(3 - 2*x)`.
  The output blends dry and controlled gain by this engagement, avoiding a
  static jump at 30%. Live engagement changes have a 2 ms full-range
  crossfade. Once fully engaged, sustained material above the knee tends
  to −3 dBFS while quieter material is lifted. Full Limit retains the prior
  −18 dBFS threshold and 15 dB makeup endpoint.
- **Attack (1–50 ms)** sets the control capacitor's response.
- **Release (20–500 ms)** sets initial recovery. A second reservoir charges
  over 80 ms and recovers over four times the selected release; 30% of its
  charge forms a floor for the main control state.
- Returning **Limit to 30% or below** settles to exact musical bypass after
  the engagement transition. Independent overload protection remains active
  at every Limit setting, so this is not an unrestricted full-scale output bypass.

The retained 1.4.0f7 / 1.5.0f1 builds used the earlier full-travel mapping:
their 5% default had a threshold near −3.23 dBFS and about 0.23 dB makeup.
Those historical values do not describe the clean 0–30% Limit range introduced in 1.5.0f2 and retained in f3.

The host-rate detector divides the stereo peak by its threshold, uses a
narrow continuous knee (±0.10) around unity, and smooths the resulting demand
with the selected attack/release coefficients. Its controlled gain is
`wetGain = makeup / (1 + control)`, with `makeup = 10^(-3/20) / threshold`.
The applied gain is `(1 - engagement) + engagement * wetGain`.
The previous instantaneous audio rail and post-decimation sample clip are
removed: even a tiny positive Limit setting previously rounded hard attacks
aggressively, creating unwanted distortion. The retained detector is a voiced
model, not a measured hardware replica.

## Output headroom

After the Main musical limiter and the independently calibrated stems, four gain controllers
anticipate peaks by **2 ms** and keep all seven outputs at or below **0.94 sample
peak** (about −0.54 dBFS), including at Limit zero. The groups are Main L/R,
Close L/R, Room L/R and mono Piezo; stereo pairs share gain to preserve their
balance. With ample headroom and no nearby
overload, it leaves audio unchanged apart from delay. The f7 fixed calibration reduces level before these detectors; loud playing
can still invoke protection.
Lookahead can attenuate the quiet samples just
before an overload; hold and recovery also affect the following decay.
It changes the gain envelope of overloaded notes; it does not promise an
unchanged waveform for every transient or an intersample/true-peak ceiling.

Each group detector takes the largest of three observations: the actual channel
sample peaks, a bounded three-sample sinusoidal estimate at local maxima, and
four-times FIR interpolation. The sinusoidal estimate reduces gain modulation
caused by a steady high tone moving against the sample grid. The FIR catches
compound transients whose peaks fall between samples and are missed by that
local sinusoidal estimate. It has three fractional phases, 32 taps per phase,
and a Kaiser window with beta 8.6. Its coefficients are fixed, generated by
`Tests/generate_output_peak_kernel.py`. Only the detector uses the FIR; audible
samples retain their original reconstruction and receive a smooth scalar gain.

For each frame the required gain is `min(1, 0.94 / peak)`. A rolling minimum
retains each requirement for `D + 1` frames, where
`D = ceil(sample_rate * 0.002)`. The control gain falls immediately to that
minimum, holds for 30 ms to prevent bass-cycle recovery, then recovers with a
60 ms time constant. A moving average of `S = max(1, D + 1 - 31)` frames
smooths the gain. Reserving 31 frames inside the existing 2 ms delay allows
the entire 32-tap FIR support to be observed before its earliest contributing
sample reaches the output. The rolling minimum still spans the full `D + 1`
audio window. At 48 kHz, the audio delay remains 96 frames and the smoothing
window is 66 frames. This avoids repairing overshoot with a sample clip.
Storage is fixed and processing allocates nothing. The FIR calculation is
skipped only when its coefficient L1 bound proves that the stored samples
cannot reconstruct above the detector ceiling.

The guaranteed sample-peak bound remains 0.94. FIR interpolation improves
protection against reconstructed peaks; a finite four-times detector and a
changing gain envelope do not establish a universal 0.94 intersample ceiling
or certified true-peak mastering behavior. Output reconstruction must also be
measured with an independent interpolator, including compound transients and
bursts, rather than inferred from the detector's own estimate.

All five direct stems have a fixed calibration, 0.65 × the per-capture,
per-family trim listed in [the output-level report](output-level.md), and no
Volume, followed by their own capture group's protection. The f7 trims are
70% of f6.
All seven channels keep the
same 2 ms delay. A direct group's detector never controls main gain or another
capture's gain. All direct stems bypass Drive, Warmth, the Main recording
calibration and the musical Limit
stage. Below the protection threshold, with no recent or nearby overload,
the protection stage preserves each direct capture apart from its delay.
The complete Room capture is independent of its mix coefficient and retains
its natural tail while muted in the main mix. Its late energy calibration
follows the room geometry and decay, as described in the
[capture reference](capture-reference.md).

The Rack controller declares the same rate-dependent latency on all seven
outputs, unchanged by Quality or Limit. Reset/idle clear the delay and gain
history. Reconnecting a direct socket clears only that channel's queued samples
and peak-estimator history, including the FIR ring and its active-history counter.
Group gain and minimum history survive reconnect,
so a queued overload on the connected stereo sibling cannot emerge at unity
gain. The capture recovers naturally. Nearly equal recurring peaks renew the hold without increasing gain;
full recovery to unity remains possible after overload ends. The quality-switch
fade is applied after the delay so clearing a
queued tail cannot interrupt a partly faded output. The existing minimum-phase
oversampling filters retain their frequency-dependent phase response.

## Compatibility and verification

Stored property names, normalized ranges, timing units and MIDI IDs remain
unchanged. Init and factory Drive are 10%, with Limit at 5% or below;
Equipentatonic Log Keys retains 0%. User controls retain their full ranges. The f3 Drive transfer and routing change older songs without
migrating stored knob positions: the complete Main mix now saturates, while direct
Piezo stays outside Drive. The inherited f2 Limit mapping makes settings through
30% clean. Its Decay/Room tapers and source calibration also affect older sounds.
Final f3 SDK build, host and release verification is incomplete. Earlier versioned releases
and their validation evidence remain untouched.

`HeadroomTest` checks quiet bit-exact delayed output on all seven channels,
independent group ceilings, linked stereo gain, overload isolation, low-frequency
gain modulation, smooth recovery, reset, pending FIR history on reconnect,
and rate changes.
`CaptureRoutingTest` covers Drive routing and independent captures; f3 requires
fresh checks of Main saturation and clean direct outputs. The existing
`RoomLevelTaperTest` checks the squared Room coefficient and invariant direct
captures. Engine, wrapper, preset, extreme-range and fuzz
contracts test these stages in context. The
[headroom and capture report](headroom-and-capture.md) records the earlier
main-only protection measurements; the following limiter-introduction results
also remain historical.

## Historical limiter-introduction results

The following 11-patch, sanitizer and CPU measurements describe the original
limiter introduction and its pinned sources. They are not measurements of the
subsequent b3 physical-model changes. Current b3 build, limiter-engagement,
factory and sanitizer evidence is recorded in
[the b3 release report](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.4.0b3.md).

The full eight-step suite passes, including all 11 factory patches at 44.1, 48
and 96 kHz, real-time allocation checks, wrapper property mappings, and UI asset
contracts. The full DSP suite also passes AddressSanitizer, UndefinedBehaviorSanitizer
and libc++ FAST hardening. [Validation receipt](../validation/limiter-validation.json),
[contract log](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/limiter-contracts.txt), and
[sanitizer evidence](../validation/limiter-sanitizer.json) pin the tested sources.

The quiet native CPU probe uses 48 kHz, 64-frame batches, 16 voices, EngineParameters
defaults, and periodic restrikes. It is an engine timing comparison, not an exact
factory-patch workload or a Reason/target performance qualification.

| Quality | Previous median thread CPU | Limiter median thread CPU | Limiter p99 | Wall / CPU deadline misses |
| --- | ---: | ---: | ---: | ---: |
| 2× (default) | 66.542 µs | 61.875 µs | 79.167 µs | 0 / 0 |
| 4× | 129.708 µs | 121.125 µs | 147.000 µs | 0 / 0 |
| 8× | 256.084 µs | 238.708 µs | 306.292 µs | 0 / 0 |

All measured blocks met the 1,333.333 µs deadline. The default median is about
7% lower in this run. Raw [baseline](../benchmarks/limiter-cpu-baseline.txt) and
[final](../benchmarks/limiter-cpu-final.txt) measurements are retained.
