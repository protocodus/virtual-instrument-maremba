# Kalimba held-body decay, 2026-10-21

Kalimba tine lifetimes are scaled to 70% of their previous values, including
the independent neighboring-tine brush. The existing Decay taper, note-off
behavior and thumb contact remain. This voices effective passive damping for
a box held in the hands; it is not a measured grip/contact simulation.

At the middle Decay setting, the actual freely evolving fundamental is fit
from 50 ms RMS windows after the initial 200 ms contact transient. At 48 kHz:

| Note | Previous fitted T60 | New fitted T60 |
| --- | ---: | ---: |
| C4, MIDI 60 | 5.268895 s | 3.688208 s |
| A4, MIDI 69 | 2.690055 s | 1.883038 s |
| C6, MIDI 84 | 0.648297 s | 0.453806 s |

Across 44.1, 48 and 96 kHz, the nine Kalimba rendered lifetime ratios are
0.6999965–0.7000000. Their first three configured modal lifetimes scale by
0.7 within floating-point rounding. The other 27 model/note/rate fixtures
retain identical close, far, piezo and bridge-output hashes. Neighboring-tine
brush poles also scale by 0.7, with other models' brush poles unchanged.

The registered `Maremba.KalimbaDecay` test passed in 1.13 seconds, including
its existing knob and velocity coverage. The regression was first run on
the old production voice and failed as expected. The [receipt](after-receipt.json),
[before data](before-expanded-tails.csv), [after data](after-tails.csv),
[red result](red-regression-result.json) and [passing log](focused-after-test.log)
retain the scope and limitations. No audio files were produced.

Shared Kalimba body/air/pickup, buzzer/collision and sympathetic resonance
poles retain their own lifetimes; their tine excitation ends sooner. The
assertion concerns the freely evolving tine fundamental rather than the
complete room/pickup tail. This focused result is not a full SDK or hosted
playback qualification. Factory levels must be remeasured after this change.
