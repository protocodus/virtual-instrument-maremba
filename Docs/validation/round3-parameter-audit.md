# Third physical-response audit

This audit uses deterministic native audio through the public engine or voice
outputs. It is not a claim of calibrated instrument measurements, listening in
Reason, or target-chip CPU performance. The frozen starting point is
`Release/validation/physical-models-round3-20260922/sources/before/DSP`.

## Reproducible findings

The old sympathetic bank covered MIDI 48–71 and included the driven key as its
strongest receiver. A pure C4 mechanical tone excited its own parallel C4
resonator: that receiver alone was only 0.68–3.55 dB below the complete halo,
whereas all other receivers together were 8.96–19.41 dB below it. This mostly
reinforced the played fundamental instead of representing unplayed neighbors.

The fixed bank also made the control nearly disappear outside those two
octaves. These levels are the direct Close difference between Sympathetic 1 and
0, divided by the dry note's energy over 0.5 seconds. All other shared layers,
noise, drift and velocity-to-control modulation were disabled; velocity was
0.85 and Position 0.35. Sample rate was 48 kHz with the default 2× internal path.

| Starting model | C2 / MIDI 36 | C4 / MIDI 60 | C6 / MIDI 84 | C7 / MIDI 96 | Worst adjacent-key step around old bank edges |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rosewood | −66.33 dB | −28.44 dB | −78.84 dB | −94.45 dB | 7.42 dB |
| Padauk | −59.60 dB | −26.48 dB | −74.30 dB | −89.39 dB | 5.87 dB |
| Balafon | −63.96 dB | −27.70 dB | −77.14 dB | −93.15 dB | 6.10 dB |
| Kalimba | −71.14 dB | −30.32 dB | −82.44 dB | −96.62 dB | 10.41 dB |

A separate quiet-restrike probe exposed a readout discontinuity. After a loud
strike, setting Coupling from 1 to 0 and restriking the same key at velocity
0.01 immediately removed the stored cavity's acoustic contribution. Two
identical voices received the same second stroke; only its Coupling setting
differed. Across MIDI 48/60/72 and 24 cavity phases at 96 kHz, the largest first
sample difference was 9.69–16.66% of the preceding Close RMS, with absolute
sample steps up to 0.03982. This exceeds the tiny onset expected from that quiet
stroke. Comparable exploratory Tune −50→+50 and Decay 8→0.1 changes produced
much smaller first-sample differences (at most 0.053% and 0.71% of RMS).

## Regression contracts

`Tests/Round3ResponseTest.cpp` adds these independent checks:

- Driving a key cannot excite the bank's duplicate of that same key. Other
  neighbors must still respond. The test observes the actual resonator output,
  not an expected coupling-list size or ordering.
- At C2, C6 and C7 the maximum Sympathetic contribution must exceed −60 dB
  relative to the dry Close signal and remain below that dry note. This is a
  conservative reachability floor, not a perceptual threshold or a demand for
  equal loudness across the keyboard.
- Around the old bank boundaries, each one-semitone change must vary halo
  level by less than 3 dB. The intended natural register trend remains allowed.
- Sympathetic changes leave the isolated Piezo samples exactly unchanged.
- A Coupling 1→0 quiet restrike may change the first Close sample by less than
  1% of the preceding ringing RMS relative to an otherwise identical restrike.
  This isolates the parameter transition from ordinary contact damping.
- All four models survive 40 strokes/second at two opposing contact corners:
  soft low notes near one end, hard treble notes near the other, with Decay 8
  and maximum Coupling, Buzz, Artifacts, Body and Sympathetic. Transport stop
  during the final contact must drain all voices within two seconds. The
  final 200 ms must be at least 70 dB below an equal-duration section of the
  roll; complete idle must follow within four seconds. These are distinct
  audible-stop and resource-release checks.

The frozen source fails 23 new assertions covering the two defects. All eight
corner-roll cases already pass: finite Close/Piezo, audible excitation, exact
zero in the late window and a fully idle engine. Existing tests already cover
velocity/position spectra, ordinary repeat variation, one low Kalimba roll,
model-change fades, cavity disconnection/tuning, contact nodes, quality changes,
parameter fuzz, and inactive-path state clearing; those contracts were retained.
The engine explicitly fades a cross-model same-key ring in its old voice and
allocates a fresh model voice, avoiding an instantaneous reinterpretation of
wood/steel modal states. No additional cross-model defect was identified.

## Integrated result

The new 61-key receiver bank covers MIDI 36–96, with at most 24 neighboring
paths per source and no self receiver. Its one-way support approximation is
still explicit: this is not a full reciprocal simulation of 128 physical keys,
and uniform response outside the core five-octave range is not claimed. A 2×
readout gain restores useful control range after removing the dominant duplicate
fundamental. The measured endpoint remains subordinate to the dry note.

| Integrated model | C2 / MIDI 36 | C4 / MIDI 60 | C6 / MIDI 84 | C7 / MIDI 96 | Largest former-edge step |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rosewood | −17.26 dB | −34.30 dB | −49.74 dB | −51.79 dB | 0.71 dB |
| Padauk | −13.59 dB | −27.24 dB | −43.15 dB | −48.77 dB | 0.72 dB |
| Balafon | −15.14 dB | −30.28 dB | −46.78 dB | −51.72 dB | 0.76 dB |
| Kalimba | −21.04 dB | −40.35 dB | −54.77 dB | −53.48 dB | 0.71 dB |

All observed self-receiver outputs are exactly zero and all direct Piezo
comparisons remain sample-identical. Cavity radiation gain now settles over
5 ms on restrikes while the physical junction disconnects immediately; fresh
strikes initialize directly to the requested setting. The isolated first-sample
Coupling discontinuity is now 0.060–0.177% of ringing RMS, below the unchanged
1% limit. All eight corner rolls pass. The slowest shared tail reaches full
idle after 2.3 seconds (Kalimba C3); it is already 156 dB below the roll during
the 1.8–2.0 second window and all voices are gone by two seconds.

That last measurement invalidated the initial new test's assumption that exact
idle must also occur by two seconds. The engine intentionally waits below
−180 dBFS for 200 ms before flushing. Separating inaudible support decay from
active voice release retains the audible-stop requirement and avoids treating
an expected low-frequency support tail as a defect.

The expanded nonself bank also exposed an obsolete existing bypass assertion:
it assumed a newly enabled resonant sum always has less energy than the steady
sum. The new transient measured 4.48 dB above steady, yet matched a bank that
had **never been enabled before** exactly over all 0.5 seconds after enabling.
Detuned receiver phases can cancel differently during build-up, so the old
energy ordering did not diagnose stale state. Only
`TestMeshAndBodyBypassNoStaleTail` in `Tests/test_dsp.cpp` was updated to demand
that exact fresh-branch comparison, retaining audible activation and the
steady/transient diagnostic levels. Both Body and Sympathetic pass with zero
sample difference. No DSP change was needed for bypass clearing.

## Evidence and reproduction

Logs retained in `Release/validation/physical-models-round3-20260922/`:

- `sympathetic-range-before.log`: initial full-keyboard and self-share probe.
- `coupling-restrike-probe-before.log`: independent coupling/tuning/decay onset
  comparison against an uninterrupted ring.
- `round3-response-baseline-fail.log`: the regression source compiled against
  the frozen baseline, including all 23 assertion failures.
- `round3-response-intermediate.log`: expanded bank before gain calibration;
  preserves the two identified coverage/idle findings.
- `round3-response-o3-pass.log`: integrated O3 regression, zero failures.
- `shared-bypass-fresh-state-pass.log`: exact fresh-branch equivalence for both
  shared layers, including the diagnostic that disproves energy ordering.

The baseline compatibility helper supplies the old first-bank-key value only
when that source lacks the new public constant. It does not bypass a contract.
The normal native build command is:

```sh
clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP \
  Tests/Round3ResponseTest.cpp DSP/*.cpp -o /tmp/maremba-round3-response
/tmp/maremba-round3-response
```

Use the frozen `sources/before/DSP` directory for both include and source paths
to reproduce the negative check. These are bounded correctness renders; no
performance measurement was made while other agents were building or rendering.
