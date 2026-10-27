# Gentle preset refinement, 2026-10-22

This pass follows the request for a slightly more organic, less muddy and less
resonant instrument while retaining the current dry, concrete character.

The shipped scope is preset-only. All `Source/DSP/` bytes remain identical to
1.5.6. Eighteen factory presets refine existing velocity routes; selected high
Body/Sympathetic coefficients are 95% of their preceding values. These are
coefficient trims, not claims of 5% less total output or shorter modal decay.
Ambient Cathedral and Midnight retain their sound controls. Hardness expression,
physical Decay, Room, Linear velocity, native 1x, Main gain law, output ceilings
and existing natural variation remain unchanged. Three authored VCA corrections
are independently verified against the same normal-playing protocol.

Normal-playing reference: MIDI60 alone and60+64, velocity100, Linear,1x,
150ms hold, two seconds observed. The equal-power mean of the two maximum300ms
rectangular stereo-RMS windows targets -22.2dBFS, with the existing -22.6dBFS
Direct Piezo reference. This proxy is separate from Reason's VU meter and the
five-second velocity80 phrase fingerprint. The existing +/-0.35dB tolerance,
Main reduction<=3dB, energy loss<=2dB, >1dB episodes<=100ms, zero musical/stem
reduction and -1dBFS Main ceiling remain unchanged.

Kalimba contact-position and fractional-release prototypes are deferred.
Retained runs found weaker low-range non-gain variation despite useful physical
bounds. The original DSP and all physical contract thresholds were restored;
no additional random parameter was added to clear a test. Prototype source,
logs and the scope decision are retained separately in task diagnostics.

The new A/B pack uses identical performances and actual version-authored
controls, with unity export gain, no normalization, no added processing and
no endpoint fade. It compares native engines, not actual Reason recordings.

The final 126 wrapper cases at44.1/48/96kHz pass unchanged safety and gentle
protection gates. All60factory/rate scores pass their unchanged +/-0.35dB
reference tolerance. Main protection reaches at most1.208dB,0.743dB energy
loss and42.23ms continuously above1dB; musical/stem reduction is zero and no
nonfinite output or safety reset occurs. Direct Piezo needs zero protection
on the reference notes. See [normal summary](normal-summary.json),
[bank verification](bank-verification.json) and the three normal CSVs.

SDK build and any host completion receipts are recorded separately. No hosted
playback, CPU qualification or Shop acceptance is implied by the native data.

The secondary velocity80 phrase has72fresh three-rate measurements. Frozen
RMS/peak centers were refreshed from those intentional preset changes; all
RMS tolerances, peak-window widths, Init anchors and assertions remain intact.
Maximum three-rate center error is0.1074dB. The updated 48kHz MainCalibration
gates pass24states. See [fingerprint audit](phrase-fingerprint-update.json).
