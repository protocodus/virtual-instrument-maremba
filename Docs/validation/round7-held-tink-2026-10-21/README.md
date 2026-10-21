# Round7 held-body tink fixture

The focused registered `Maremba.Round7BalafonKalimba` test passes **1/1 in 3.40 s**.
The F#4 natural-thumb guard remains **−30 to −17 dB relative to the fundamental**.
Only its held-voicing observation window changes; all other measurements,
protocols, bounds and safety assertions remain unchanged. No production DSP,
factory XML or gain changes were made.

The original 80–120 ms measurement is still printed as a historical recorded
observation. It reports −31.5855 dB after the intentionally shortened modal
lifetimes and caused the retained focused failure. The asserted held-voicing
comparison uses an independent fixed 0.7 times those observation times:
56–84 ms, reporting −27.4936 dB. The historical specimen's holding condition is
not established by the repository. This is an engineered normalized recorded-age
comparison, not a claim of measured free-versus-held or exact modal-age equivalence.

## Bounded timing and latency check

A single natural-thumb F#4 fixture was rendered from exact committed source
exports before and after the 0.7 lifetime change. The original Round7 `Render`
and `PeakLine` helpers are unchanged; their full test main was renamed and never
run by the scratch probe.

| Observation | Before | Held |
| --- | ---: | ---: |
| Historical 80–120 ms | −27.66710085 dB | −31.58546766 dB |
| Fixed 0.7 recorded-age window | −24.79346189 dB | −27.49357260 dB |
| Illustrative tine-delay-adjusted window | −24.90175883 dB | −27.64835427 dB |

The complete Close recording carries 2 ms of common output delay and acoustic
flight of 1.732917 ms for this tine, 1.804580 ms for the board and 1.762643 ms for
the hole. It also carries residual ITD and frequency-dependent minimum-phase ear
filters. The Close convolution compensates its partition processing delay within
the declared flight delay. No added hidden latency was assumed.

For a *single* delayed source, equal source age would use `L + 0.7 * (t - L)`.
Using the known tine flight plus output delay gives an illustrative window of
57.119875–85.119875 ms, 1.119875 ms later than the requested fixture. Its held ratio
is −27.64835427 dB: only **0.154782 dB** from the requested recorded-age measurement,
and 0.018747 dB from the previous historical measurement. The requested result
has 2.506427 dB of margin above the unchanged lower guard.

This sensitivity is immaterial to the existing guard for this exact fixture.
The test therefore keeps the requested recorded-time normalization and labels
its approximation; it adds no unsupported exact latency formula. The coherent
three-emitter recording and its ear filters do not have one exact shared delay.
The default fixture is native 1x; no oversampling resampling delay is involved.

The existing `PeakLine` helper truncates floating observation times to integer
frames. The probe retains those actual endpoints (2687–4031 at 48 kHz for
0.7 times the historic endpoints), rather than silently changing that protocol.

The [before failure](before-ctest.log), [passing log](focused-ctest-detail.log),
[raw timing observations](held-time-probe.txt), [probe receipt](time-probe-receipt.json),
[source exports](probe-source-exports.json), [input verification](final-input-verification.json)
and [receipt](receipt.json) retain the exact metrics, commands, source hashes and
limitations. Independent read-only review found no actionable issues. The existing
native configure warning concerns unavailable Python scientific packages; the
selected C++ test was registered and passed. A fresh full SDK preflight remains
necessary after these focused checks.
