# Strike Variance held-age fixture

The single registered `Maremba.StrikeVariance` test passes **1/1 in 1,629.41 s**,
with all **2,304** original strike/replay lifecycle cases. No numerical threshold
was lowered. All three bar-model protocols retain their original 40–160 ms
observation window; all 18 printed bar checkpoint results match attempt 3.

The sole earlier failure was Kalimba's 5% physical median, 0.00354682, below the
existing 0.005 minimum. Its original 40–160 ms values remain printed for every
checkpoint. Only Kalimba's asserted pressure residual uses an independent fixed
0.7 recorded-age factor, giving 28–112 ms after the approved 30% shorter modal
lifetimes. This is an engineered normalized recorded-age comparison, not exact
modal-time or measured specimen equivalence. Body, preload and capture timing
remain unchanged.

## Bounded pressure evidence

Exact DSP exports from `26debb9` and `80e5277` rendered only the existing C4
48-hit ongoing sequence at Variance 0 and 5. Those same clips supplied both
windows; no fresh engine per strike or alternate PRNG sequence was introduced.
The original render/drain helper and its 3 s voice / 12 s engine deadlines ran
unchanged. Both variants drained 96 hits without lifecycle failures.

| Complete Kalimba L+R residual | Variance 0 | Variance 5 |
| --- | ---: | ---: |
| Old DSP, historical 40–160 ms | 0.000640082883 | 0.005557758441 |
| Held DSP, historical 40–160 ms | 0.000408527919 | 0.003546824931 |
| Held DSP, normalized 28–112 ms | 0.000660667572 | 0.005736848018 |

Normalized held pressure is only 3.216% / 3.222% above the corresponding old
historical measurements. The 5%-to-natural-floor ratio stays 8.682873 versus
8.683411. Stereo Close follows the same comparison: the old 5% historical
residual is 0.005588737853; held normalized is 0.005770049276. Thus the comparison
restores a similar non-gain pressure signature across both the natural floor
and low control setting, rather than choosing a weaker pass threshold.

Gain-only 48-hit controls varied scalar level from 0.8 to 1.2 on one fixed
waveform. Every mono/stereo residual in both windows and both DSP versions is
below 0.000000191, passing the unchanged 0.000001 negative-control limit. The
final registered test also retains its original per-checkpoint 1.2 scalar-gain
controls, evaluated over the same windows as the asserted timbre observations.

The fixture uses the default Natural Thumb technique, whose release is about
0.2 ms at this setting. The 28 ms start remains well beyond that excitation;
the observation still includes the complete board/hole/capture pressure. This
does not claim those unscaled shared paths or the 40 ms preload bleed have been
time-compressed. The comparison is limited to the explicit recorded-age policy.

## Preserved checks and evidence

All original absolute and relative residual thresholds, six amount checkpoints,
48-hit uninterrupted PRNG sequence, reset replay equality, 0–40 ms attack-level
span, finite output, silence, 3 s damping and 12 s drain requirements remain.
Physical and stereo residuals still remove the best scalar gain. The registered
test passed every checkpoint and lifecycle check with the new Kalimba-only age.

The [failed attempt excerpt](before-failure-excerpt.log), [raw old data](before-diagnostic.txt),
[raw held data](held-diagnostic.txt), [comparisons](comparisons.csv),
[bounded diagnostic receipt](diagnostic-receipt.json),
[full focused log](focused-ctest-detail.log), [source verification](final-input-verification.json)
and [receipt](receipt.json) retain inputs, commands, source hashes and limitations.
Only `Source/DSP` and this test were watched during the focused check;
`FactoryPresets.h` is unrelated to this engine-only fixture and was concurrently
owned by the bank-trim agent. No production DSP edits, audio files, host runs or
additional matrix runs were made. Independent review found no actionable issues.
A fresh full SDK preflight remains necessary.
