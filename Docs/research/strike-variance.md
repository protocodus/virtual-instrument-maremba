# Strike Variance — smooth response across the full range

## 1.4.0b1 (combined)

1.4.0b1's strokes are physical: head stiffness and loss, stick speed, landing
point and lateral offset, drawn once per strike. They keep this page's smooth
curve, `a (2 − a)` on the added spread, and widen the maximum so the control
is as audible as the 1.3.0f5 response ([combined models](combined-models-1.4.0b1.md)).

| Character component | Bound at 0% | Bound at 5% | Bound at 15% | Bound at 100% |
| --- | ---: | ---: | ---: | ---: |
| Head stiffness (contact duration) | ±3% | ±6.41% | ±12.71% | ±38% |
| Stick speed | ±0.4% | ±1.18% | ±2.62% | ±8.4% |
| Landing point (of the length) | ±0.12% | ±0.80% | ±2.06% | ±7.12% |
| Lateral offset (of the half width) | ±0.25 | ±0.279 | ±0.333 | ±0.55 |
| Head loss (release shape) | ±2% | ±3.46% | ±6.16% | ±17% |

`Tests/StrikeVarianceTest.cpp` on the combined engine (twelve C4 hits, 48 kHz,
as below):

| Variance | Median adjacent pitched-waveform difference (Rosewood, Padauk, Balafon, Kalimba) | Attack-level span |
| --- | --- | ---: |
| 0% | 0.31, 0.47, 0.32, 0.09 % | 0.08–0.21 dB |
| 5% | 0.55, 1.00, 1.18, 1.24 % | 0.18–1.08 dB |
| 15% | 1.16, 2.39, 2.95, 2.46 % | 0.36–2.75 dB |
| 100% | 3.48, 7.18, 9.44, 6.21 % | 1.11–3.60 dB |

Rosewood varies least in the pitched window: its tube carries the ring's
fundamental, a scalar the metric removes. The sections below describe 1.3.0f5.

The current source removes the 25% threshold from the added strike expression.
The earlier 1.3.0f2–f4 curve retained the old subtle response through 25% to
preserve low settings, then expanded quadratically above it. That made ordinary
settings such as 5% and the default 15% too restrained. The new curve expands
contact duration, release shape, impulse strength, shock strength and partial
balance immediately, with no threshold or piecewise knee.

For normalized amount `a`, the added response is `a * (2 - a)`. It rises smoothly
and more quickly through the lower settings, then eases toward the unchanged
maximum. The small existing linear contributions remain. At 5%, the additional
response is 9.75% of its maximum; at 15%, it is 27.75%.

| Character component | Bound at 0% | Bound at 5% | Bound at 15% | Bound at 100% |
| --- | ---: | ---: | ---: | ---: |
| Contact/release duration | ±0.75% | ±3.875% | ±9.675% | ±34.75% |
| Pulse skew offset | ±0.006 | ±0.017 | ±0.0375 | ±0.131 |
| Impulse strength | ±0.35% | ±1.375% | ±3.275% | ±11.35% |
| Shock strength | 0% | ±3.7125% | ±10.6125% | ±41% |
| Individual partial strength | ±0.4% | ±4.475% | ±12.025% | ±43.9% |

These are generator bounds, not typical changes or perceptual scores. The
three-draw distribution concentrates hits near the nominal value, and the
voice's physical clamps still apply. The separate torsional spread continues
to scale linearly across the whole knob.

## Compatibility and behavior

The curve change alone preserves the zero/maximum character bounds, random
sequence and rendered audio exactly. All intermediate settings become more expressive,
including existing factory patches and songs. Stored values, defaults, property
identity and automation are unchanged. This change is included in the
[1.3.0f5 release candidate](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.3.0f5.md), with freshly rendered
showcases. F5 also changes the instrument geometry, so complete f4 and f5
renders can differ at every setting, including zero and maximum. The frozen
f4 U45, release ZIP and showcase audio remain unchanged.

Each new strike, including a restrike, receives its own character. Moving the
knob does not modify an already ringing note. The character generator adds no
pitch or longitudinal strike-position randomness. Its fixed note-on random
draw count, deterministic Reset and lack of audio-rate work remain unchanged.
Zero retains the subtle [organic strike floor](organic-strikes.md).

## Validation

The measurements below isolate the curve change on the frozen geometry before
the fifth physical-model pass. The same regression gates also pass on f5, but
these historical numbers are not fresh f5 measurements.

`Tests/StrikeVarianceTest.cpp` renders twelve identical C4 hits per instrument
at 48 kHz / default 2x quality, damping fully to silence between hits. Room,
Artifacts, Buzz, Bloom, Sympathy, Drive and compression are off. Best-fit gain
is removed before comparing the pitched output at 40–160 ms, so an attack-noise
burst or gain change alone cannot satisfy the timbre check.

| Variance | Median adjacent pitched-waveform difference, across instruments | Attack-level span in each twelve-hit phrase |
| --- | ---: | ---: |
| 0% | 0.157–0.258% | 0.046–0.051 dB |
| 5% | 0.785–1.278% | 0.410–0.427 dB |
| 15% | 1.973–3.159% | 1.107–1.151 dB |
| 25% | 3.031–4.854% | 1.739–1.806 dB |
| 50% | 5.132–8.240% | 3.022–3.135 dB |
| 100% | 7.044–11.316% | 4.194–4.348 dB |

The pitched-timbre measure grows about 4.7–5.0 times above the natural floor at
5%, and 11.6–12.6 times at 15%. These numerical comparisons establish increased
variation in this probe; they do not guarantee that every adjacent pair is
perceptually distinguishable in every performance.

Regression checks cover all four instruments at 0/5/15/25/50/100%, exact Reset
replay at every checkpoint, unchanged generator endpoints, fixed random draws,
bounded character and increasing ensemble spread at every 1% step. Running the
new contract against the frozen previous curve fails the low-setting gates
at 5% and 15% for every instrument, with no other failures.

For the retained curve-only listening comparison, `Demos/compare_variance.py`
built the two frozen pre-round5 DSP versions and rendered eight equal-velocity
C4 hits at each checkpoint
for all four instruments. All WAV files share one fixed gain; there is no
per-clip normalization or compression. All eight zero/maximum float-render
pairs are bit-identical in that historical comparison. F5 additionally includes
the geometry changes, so endpoint identity is no longer expected across those
complete versions. Phrase attack-level measurements
include ringing tails; the isolated test above is the stronger timbre evidence.

Evidence is retained under `Release/validation/variance-smooth/`, including
`strike-variance-contract.txt`, the previous source snapshot and hashes, and
`comparison/variance-review.html` with its source-hashed JSON report.
Reproduction requires the frozen pre-round5 source hashes in that report.
The script deliberately requires endpoint identity; comparing the historical
baseline directly against f5 will fail that gate. Use
`Demos/build_physics_review.py` for the subsequent geometry comparison.

Historical f2/f3/f4 release evidence describes the old thresholded response and
remains a record of those immutable packages.

The full local suite passed, including DSP physics, demo-renderer contracts,
the Rack wrapper and all eleven patches at 44.1/48/96 kHz. The retained
[measurement report](../validation/strike-variance-smooth.json) includes current
and previous isolated measurements, endpoint hashes and the native CPU probe.

In two alternating before/after timing runs at 48 kHz, 64-frame blocks and
16 voices, the default 2x setting had comparable median charged-thread times
(before 165.75–167.63 µs; after 161.96–169.25 µs) with zero charged-thread
deadline misses. The 4x setting also had zero charged-thread misses. The optional
8x stress probe recorded nine charged-thread misses after the change and none
before it; that cause is unresolved. Wall-time misses occurred too, and all
runs are retained. This probe does not establish live Reason performance or
that the 8x stress path is unaffected.
