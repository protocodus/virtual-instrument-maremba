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
