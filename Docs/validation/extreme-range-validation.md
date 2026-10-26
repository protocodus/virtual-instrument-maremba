# Extreme-note and chord validation

`Tests/ExtremeRangeTest.cpp` is a deterministic functional contract registered
in `Tests/contracts.txt`. It complements the seeded state-machine fuzz test;
its cases are reproducible without relying on a random search.

As of 8 October 2026, the default is a **996-case compact matrix**. It retains
representative note/velocity/rate axes, low-pluck regressions, all quality
settings, hostile notes, limiter transitions, transport drains and four exact
32/64-frame chord comparisons. `--full` explicitly selects the historical
12,140-case workload. See [test profiles and commands](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Tests/README.md).
All continuous-control min/mid/max positions and every discrete state are
enumerated separately by the compact engine control test.

The description and measurements below record the original September full
matrix, including its then-current output policy. They do not describe today's
always-active 0.94 protection on all seven outputs or qualify a later package.

## Historical full-matrix coverage

The full matrix covers:

- All four instruments, all MIDI notes 0–127, velocities 1/127, 0.5 and 1,
  and four combinations of minimum/maximum physical controls at 48 kHz/2x.
- Every MIDI note at 22.05, 44.1, 48, 96 and 192 kHz with the fully coupled,
  loose endpoint combination at 2x. A separate boundary-note grid covers
  every rate, all three velocities, and 2x/4x/8x quality.
- Invalid MIDI integers, including integer limits: they must be ignored
  without modifying a valid held note. Valid MIDI notes must retain their
  identity even where the physical geometry uses its boundary calibration.
- Low Kalimba plucks with fixed technique/tuning selectors, both Carving and
  Buzz endpoints, Free and Dead strokes, all rates, and quality boundaries.
  This isolates static thumb preload and the later thumb-return/buzzer event.
- Dense 24-key chords spanning MIDI 0–127, including adjacent extreme bass
  notes. These cross all four instruments, 8/16/24-voice settings, minimum and
  maximum limiter attack/release, and Drive 0/1. They include restrikes, voice
  stealing, live limiter bypass/engagement, changes to limiter ballistics,
  note-off, transport damping, and rate/quality corners.

Each ordinary note retains a 150 ms held onset before live Tension, note-off
and transport actions. The focused low plucks retain 200 ms: the initial
release-hardening investigation found defects occurring 33–79 ms after the
strike, beyond a short attack-only check. Transport drains have a 30-second
bound. Dense-chord output hashes must match between 32- and 64-frame host
batches for each instrument over the same 400 ms event sequence.

All seven outputs must be finite, fully written and confined to their requested
buffers. Active/live voice counts must respect the pool and selected polyphony.
The engine clock detects output-guard resets. Low-pluck and recent low-chord
voice survival checks also detect per-voice guard kills that leave the engine
clock running. Reset and ignored-note renders must be exactly silent.

The main output must remain at or below 0.980002 when the limiter is fully
engaged, including its first configured sample and the first sample after
live engagement. A separate 180-case limiter check crosses all internal rates,
minimum/default/maximum enabled amounts, and attack/release endpoints.
The attenuation ramp still spans 2 ms; its output retains the strict requested
ceiling and the transition is measured separately.
Bypassed main and direct outputs are measured without imposing the limiter's
ceiling on those paths. A peak bound of 32 in the fixed low-pluck fixture guards
against the previously pathological excitation; it is **not** a claim of safe
playback level or normal audio headroom. The matched ordinary-register fixture
measured a peak of 8.72 before the fix. Factory-preset headroom has a separate
contract.

The JSON summary reports coverage, output peaks with exact case identities,
limiter transition peaks, adjacent-sample steps, voice counts, mechanical
energy for the low-pluck fixture, transport drain duration, and elapsed time.
Large sample steps can be intentional percussion or saturation; this test
does not classify them as clicks or aliasing. Exact batch agreement verifies
scheduling consistency, not perceptual quality. These native tests do not
establish a host CPU budget, Reason/Recon behavior, subjective sound quality,
true-peak limiting, or every possible Cartesian combination of controls.

The release run uses a 900-second process timeout and records SHA-256 hashes
of all DSP sources and this contract before compilation and after execution.
Raw logs and failed intermediate fixtures are retained under
`Release/validation/release-hardening-1.4.0b3/extremes/`.

## Recorded runs: 26 September 2026

The final production source passed the complete 12,140-case matrix during
local45 preflight in 532.6 seconds, below the 900-second contract timeout.
All DSP and ExtremeRangeTest source hashes matched the pinned final inputs.
The portable result is `Maremba_Extreme_Validation.json`; the exact extracted
contract log and full source map are in `extremes/local45-final-report.json`.
The table below describes that final-source full run. The universal45 preflight
repeated the entire matrix in 402.1 seconds; every reported metric except
execution time matched the native run.

Earlier strict-full and 5,056-case affected-velocity reruns remain preserved in
`extremes/combined-report.json`. They are supporting development evidence,
separate from the new complete execution on the final bounded-slip source.

| Measurement | Result |
| --- | ---: |
| Cases / output frames | 12,140 / 205,351,632 |
| Engine output-guard checks | 3,224,444 passed |
| Targeted low-pluck survival checks | 463,312 passed |
| Dense chords / exact batch comparisons | 116 / 4 passed |
| Standalone live limiter engagement combinations | 180 passed |
| Maximum active / live voices | 24 / 24 |
| Longest transport drain | 19.724 s |
| Fully engaged limiter peak | 0.980000079 |
| Live limiter engagement peak | 0.980000079 |
| Bypassed main peak | 156.088 |
| Direct Close L/R peaks | 6.69358 / 3.36290 |
| Direct Room L/R peaks | 6.17310 / 2.61127 |
| Direct Piezo peak | 1.12948 |
| Focused low-pluck peak / sampled mechanical energy | 20.4008 / 6200.14 |
| Largest adjacent main-output sample step in chords | 21.8912 |

The high bypass and direct-output figures come from deliberately
maximized gain/control combinations. They demonstrate that the passing
numerical tests do not establish normal listening headroom on those paths.
The largest bypassed main peak was Rosewood at 192 kHz/2x, MIDI 102, velocity 1,
the loose-coupled endpoint profile, frame 19384. The fully engaged limiter
remained within its separate sample ceiling, including live engagement.
Immediate protection of an already hot bypass signal can cause an abrupt
level change at the enable boundary; no click-free or alias-free claim is
inferred from the sample-step statistic. The final-source velocity-only rerun
rendered 45,007,040 frames and passed 695,936 engine-guard checks; its enabled
limiter maximum was 0.980000019.

Earlier evidence remains preserved. The initial low Kalimba fixture exposed
unphysical static thumb travel and later buzzer energy-guard kills, fixed in
the physical excitation/calibration rather than by clipping its outputs.
The `pre-strict-limiter-*` run recorded a live-enable peak of 23.5443 during the
former interpolated ceiling; the stricter engagement contract above catches
that behavior. A separate three-second worst-case Rosewood probe found bounded
mechanical energy, no resets and sustained high bypass level from repeated
rolls; disabling rolls let the isolated strike decay. Raw traces remain in
the same evidence directory.

## Artifact-detail contract calibration

The intentionally quieter artifacts exposed an obsolete universal loudness
floor in `EngineRealismTest.cpp`. Its deterministic 64-hit Kalimba ensemble
measured -20.3749 dB wet-minus-clean energy relative to the clean tine, narrowly
below the former -20 dB requirement. This is about 9.6% relative RMS change;
86.7% of the added energy remains in the mechanical low band and 79.6% occurs
after the initial impact. The failure did not indicate an inactive control,
added broadband noise, or unstable output.

Only Kalimba's minimum changed to -24 dB, still requiring about 6.3% relative
RMS change. The bar instruments retain their -20 dB floors. Finite/bounded
output, deterministic replay, the 45% low-band minimum, 20% delayed-energy
minimum, pickup distinction, parameter response and sample-rate invariance
gates are unchanged. The complete affected contract now passes; its maximum
wet-minus-clean artifact rate error is 0.247 dB against the existing 1 dB bound.

No DSP changed for this adjustment. The before/after source manifests and logs
in `extremes/artifacts-contract/` reproduce the single original failure and
subsequent zero-failure result. `comparison.json` confirms unchanged production
hashes and identical measured-audio result lines. This calibrates the minimum
to the requested subtle detail without increasing artifact loudness in the
instrument or dropping the independent noise and safety checks.
