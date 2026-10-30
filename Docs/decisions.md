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
