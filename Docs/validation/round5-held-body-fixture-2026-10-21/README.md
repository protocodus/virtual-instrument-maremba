# Round5 held-body fixture correction

The focused registered `Maremba.Round5Response` test passes **1/1 in 28.07 s**.
Its rendered re-pluck requirement stays **below −25 dB**, using the same C4,
velocity 0.8, 48 kHz Close output and 1.05–1.6 s energy window. No production DSP,
levels, factory values, unrelated bounds or other measurement protocols changed.

The first full preflight failed three assertions: re-pluck residual −22.408 dB,
C4 T60 3.6882 s, and middle-register median T60 1.883 s (range 1.6465–2.9401 s).
The retained [failure excerpt](before-failure-excerpt.log) identifies that attempt.

## Re-pluck subtraction

Natural strike-character variation remains even at Variance zero. The old
fixture subtracted a first-strike fresh sound from a second-strike re-pluck,
which mixed different new excitation into the supposed old-ring residual.
The shorter old ring made that comparison more sensitive to this difference.

A bounded probe used exact committed DSP exports from `26debb9` and `4389cd4`.
All energies below sum squared `(closeLeft + closeRight)` samples over the same
late window; they are neither stereo RMS nor Main-output levels.

| Measurement | Previous lifetime | Held lifetime |
| --- | ---: | ---: |
| Counterfactual old-ring energy | 3.113429997 | 0.769068211 |
| Original residual energy | 0.006637886 | 0.004417234 |
| Original residual ratio | −26.712093 dB | −22.408144 dB |
| Matched-excitation residual energy | 0.000221666 | 0.000056518 |
| Matched-excitation residual ratio | −41.475395 dB | −41.337803 dB |
| First-thumb-sample old modal-memory suppression | −41.938200 dB | −41.938200 dB |

The old-ring denominator falls 6.072742 dB; the original residual numerator
also falls, by 1.768793 dB. Matching the new pluck's PRNG sequence removes the
unrelated excitation difference. The production test now captures the actual
engine PRNG immediately before its second strike and supplies it to the fresh
counterfactual immediately before that strike. No old voice, body or capture
state is copied into the fresh engine. The unchanged rendered −25 dB guard
therefore checks old ringing memory with matched new excitation.

The separate first-thumb-sample trace holds new-pluck excitation identical and
zeros only the old pitched state in one clone. Its quadratic modal-energy
difference confirms unchanged suppression at −41.938200 dB in both versions.
The natural thumb's production retention coefficient remains 0.008.

A bounded [negative control](negative-control.log) changes only that coefficient
to 1.0 in a scratch source export. The exact edited canonical re-pluck block
then reports −0.0000000715 dB and **fails the unchanged −25 dB requirement**, exit 1.
The fixture detects a thumb that retains the old tine motion.

## Decay ranges

Historical Holdaway-inspired reference endpoints remain explicit. The repository
does not establish their specimen's holding condition, so these are not labeled
measured free-versus-held ranges. The approved held-body voicing is separately
derived as 0.7 times those reference lifetimes, including the top-register bound.

| Quantity | Historical reference, seconds | Held expectation, seconds |
| --- | ---: | ---: |
| C4 | 4–5.5 | 2.8–3.85 |
| E4–B4 median | 2.5–4.5 | 1.75–3.15 |
| Lowest middle-register value | >2 | >1.4 |
| Highest middle-register value | <4.8 | <3.36 |
| E6 | 0.2–0.9 | 0.14–0.63 |

The test's 0.7 factor is fixed independently of production code. Other models,
windows and bounds remain unchanged. The separate Kalimba rendered/modal
regression verifies the actual 70% lifetime change.

The [receipt](receipt.json), [focused log](focused-ctest-detail.log),
[before data](before-diagnostic.txt), [held data](held-diagnostic.txt) and
[diagnostic receipt](diagnostic-receipt.json) retain commands, hashes, input
verification and limitations. Small scratch setup/compile failures are recorded
in the diagnostic receipt; no probe executed before their correction. A fresh
full SDK preflight remains required after this focused result.
