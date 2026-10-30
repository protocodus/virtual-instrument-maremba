# Maremba — decision log

Directions chosen by ear, recorded per the A–Z listening-test convention in
the repository's `CLAUDE.md`. A choice made by ear is recorded as made by ear:
it does not close a calibration question that only a measurement of a real
instrument would.

Most of Maremba's modelling rounds were decided by measurement and say so
(for example [round 6](research/physical-models-round6.md): "nothing here is
a listening test"). The one direction recorded as chosen by listening so far
predates this log.

## 1.4.0f7 — output calibration

The shipping output level follows **listening option 02**, then reduced from
its 80% calibration to 70% (a further −1.16 dB), as
[output calibration](research/output-level.md#140f7-70-output-calibration)
records. The option set and its key were kept with the Rack device's evidence,
not here, so this log cannot say what the other options were. What the choice
licensed: the 70% post-limiter trim on Main and every direct capture, and the
0.05 Drive and Limit cap on Init and the factory patches. It is a level and
balance choice, not a measured acoustic fact.

## 1.5.8 — upper overtones, mallet core, repeated notes

Three changes, each with an A/B set (A the 1.5.7 engine, B the change alone,
RMS-matched), sent for review and not committed:

- **Upper overtones** and **mallet core** were settled by measurement against
  the Iowa MIS 2012 concert marimba (`LossFactor` and `GetMalletSpec` in
  `Source/DSP/MarimbaModel.h` give the numbers). The A/B sets are review
  evidence, not a choice between defensible candidates.
- **Repeated notes** is a playing-model choice with no measurement behind it:
  a key struck again within the slowest roll's stroke interval alternates
  mallets at the roll's spacing. B ships under the project's standing rule that
  the candidate wins unless the owner objects; no one has listened yet. If the
  owner prefers A by ear, record it here and revert `MarembaEngine::NoteOn`.

Tried and dropped: re-pinning the dead stroke's hold force and grip to the
Iowa dead strokes. The model's bass dead strokes choke about 8× faster than
the recordings' and its treble about 1.5× slower, but no pair of the two
constants moved the register error steadily (grid 0–4 N s/m × 5–20 N), so
the 5 N and 4 N s/m inferences stand.

## 1.5.10 — forte strokes on centre; cord friction tried

**Position VEL at 0%** on thirteen bar presets is a preset change settled by
measurement (an A/B set, A 1.5.9 and B the change alone, RMS-matched, was
sent for review and not committed): with 10% a velocity-127 stroke landed
0.1 of the bar off centre, where the antisymmetric 4f mode is strongly
excited, and the ff 4f partial sat 15–30 dB above the Iowa players' at
G3–C5; at 0% it is 8–12 dB closer (C3 overshoots by 4–9 dB the other way).

Tried and dropped: letting the cords take energy only where a mode moves at
them, so a fundamental supported at its nodes loses nothing to them. Against
the Iowa ff fundamentals it moved seven keys closer (C2 3.9 → 4.75 s against
4.7, F2 3.3 → 3.7 against 3.9, D3 2.3 → 2.4 against 2.6), but the recorded A2
bar rings unusually short (2.26 s, between F2's 3.7 and C3's 3.0), and
`Maremba.TubeInSitu` then read A2 at 1.36× its recording against a 1.35× bound.
The bound stands; the change would need the owner's call.

Looked at and left: the bass 4f and 10f partials ring about twice as long as
the recordings' (C2–G2), but no support-friction law fitted C2, G2 and C3
together.

## 2026-10-30 — in-situ decays across the keyboard (nothing shipped)

Measured every key C2–C5 of the Iowa MIS 2012 concert marimba (cord, yarn and
rubber at ff; the three mallets agree within a few per cent per key) against
Concert Grand Rosewood at velocity 112, each partial band-passed and fitted
from −3 dB to −30 dB or 10 dB above the noise. Medians of model ÷ recording:
fundamental 0.86, 4f 1.26, 10f 1.24 (4f 1.5–1.7 at C2–G2).

Tried and dropped: a dashpot for the cords' felt that damps each mode by its
mean-square motion over the felt (the fundamental, whose nodes the cords sit
on, barely changes). At 5 N s/m it brought the 4f and 10f medians to 1.01 and
1.04, but the bar's own C4 4f then rings 0.19 of its fundamental, under the
0.2 floor `Round4Response` holds from bare-bar measurements (Chaigne &
Doutaut 1997; Hill 2011: 0.25–0.6). The shipping engine sits at 0.22, so any
in-situ loss on the bar's own modes large enough to matter crosses it.

Held for the owner: weaker tube coupling (κ 0.0071 → 0.0055) or a lossier
tube gap (`kTubeGapQ` 0.45 → 0.30) each bring the fundamental median to 1.0.
Both fail `TubeInSitu` on A2 only (1.48× against a 1.35× bound): the
recorded A2 rings 2.27 s by that test's own method, between Ab2's 3.18 s and
Bb2's 3.88 s, so the anchor sits on an unusually short key.
