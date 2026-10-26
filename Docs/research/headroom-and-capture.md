# Headroom and capture correction

Historical report for the earlier main-only correction. The subsequent
[source-level and all-output correction](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/hardest-output-2026-09-27/README.md)
supersedes its delay-only direct stems and three-sample peak detector. The
measurements below describe their original pinned sources.

This update follows the packaged 1.4.0f3 build. It changes the sound of loaded
patches while preserving all property identities and ranges. The earlier
release archives remain unchanged.

## Diagnosis

The f3 engine was finite and stable in the reproduced cases. At Drive zero its
preamp did not saturate, but its unprotected main mix exceeded audio full scale.
The physical bar impact has a much higher crest factor than the ensuing ring.
The 2× main calibration made normal playing usefully loud but left large strikes
and coherent chords dependent on the limiter to avoid clipping downstream.

Measured at 48 kHz / 2× with factory physical settings and factory Volume,
but Drive and Limit both zero, a velocity-127 eight-note chord
`48, 52, 55, 60, 64, 67, 72, 76` with Close alone reached:

| Model | Main sample peak | Peak above full scale |
| --- | ---: | ---: |
| Rosewood | 6.500 | 16.3 dB |
| Padauk | 6.603 | 16.4 dB |
| Balafon | 11.608 | 21.3 dB |
| Kalimba | 1.667 | 4.4 dB |

This was not confined to extrapolated notes. Balafon single notes in MIDI
36–84 also exceeded full scale. Removing artifacts, Bloom and Sympathetic did
not remove the large initial bar transient. With Limit at only 0.01, the old
instantaneous peak rail alone could remove roughly 19–20 dB from a forte
Rosewood/Balafon chord, strongly reshaping its attack. Drive also applied to the
entire main mix, contrary to the requested pickup-only behavior.

## Correction

- Drive processes Piezo before both its mix coefficient and direct output.
  Close and Room are unchanged by Drive. Zero Drive is an exact bypass of the
  saturation transfer; the existing physical pickup/circuit model still runs.
- Close, Room and Piezo remain independent linear coefficients. Room now
  scales its complete capture once, removing the previous squared wet-return
  scaling. Muting Room in the main mix preserves its direct capture and tail.
- Main keeps the useful 2× calibration. The limiter's instantaneous rail and
  final sample clip are removed. Its musical attack/release control remains.
- A separate smooth gain controller anticipates overloads by 2 ms after
  decimation and Volume. It remains active at Limit zero, with a 0.94 main
  sample-peak ceiling. All direct stems receive the same delay only, retaining
  their independent fixed trim. This protection changes overloaded envelopes;
  it is not a promise of an unchanged transient or a certified true-peak ceiling.
- Nineteen of twenty presets have Detune exactly zero. Midnight Village Ritual
  keeps a deliberate maximum ±2-cent spread. Tuning-scale selection is unchanged.

The [dynamics description](analog-limiter.md) documents the gain controller and
latency contract. Physical strike velocity, microscopic hit variation and the
underlying instrument simulation remain intact.

## Verification

Baseline probes use the committed f3 sources at
`e7adf2e43c6bbe40056b5e6cdc5ffb26a87f819b`, independently copied before editing.
Raw diagnostic files are under `Release/validation/headroom-2026-09-26/`.
The [tracked measurement receipt](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/headroom-2026-09-26/README.md)
records the 112 baseline/current comparisons, source hashes and reproduction
instructions. All current main samples stayed within 0.94; four-times offline
reconstruction peaked at 0.943009. The four moderate factory-reference phrases
changed by −0.205, −0.210, −1.013 and −0.282 dB RMS. This is unweighted RMS,
not a listening verdict or a certified true-peak measurement.

The full-range contract passed 12,140 cases, 15,012 note starts and 175,948,016
output frames, including all MIDI notes, four models, supported rates and
quality modes, dense chords, extreme controls and limiter transitions.
The largest main sample was 0.940000057, within floating-point tolerance.
Direct stems intentionally bypass this gain control and can exceed full scale
under hostile settings; their independent stability bounds also passed.

All 20 factory patches passed at 44.1, 48 and 96 kHz, including the unchanged
−22 to −12 dBFS phrase-RMS window and forte direct-output ceiling. One patch,
Equipentatonic Log Keys, received a documented level recalibration. The
24-seed engine fuzz run, wrapper/event/reset/cabling contracts, physical
contracts, generators and structural checks passed. AddressSanitizer and
UndefinedBehaviorSanitizer passed the new headroom and capture-routing tests.
The [local gate manifest](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/headroom-2026-09-26/local-gates.json)
distinguishes the initial failed fixture runs from their corrected passing
runs; completion was assembled from these runs, not a single uninterrupted
invocation of the full-suite script.

The [native timing probe](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/headroom-2026-09-26/performance.txt) used
48 kHz, 64-frame blocks, 16 voices and 4,096 measured blocks per quality.
Default 2× and optional 4× had no wall/thread-CPU deadline misses. At 2×,
wall p99 was 298.584 µs and maximum 426.959 µs against a 1,333.333 µs deadline.
Optional 8× had 21 wall and six thread-CPU misses; this stress result is
retained and is not a performance pass. Native checks do not establish
interactive Recon behavior or target-chip performance. The existing live
Recon installation and packaged f3 archives were not replaced by this task.
