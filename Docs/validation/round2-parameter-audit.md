# Second physical-response audit

This bounded follow-up used the frozen first-round DSP under
`Release/validation/physical-models-round2-20260922/sources/before/DSP`, then
the revised engine. `Tests/Round2ResponseTest.cpp` adds public-audio checks and
an isolated radiation-readout bypass;
the first round's existing contracts and limits remain intact.

## Confirmed discontinuity and correction

The former upper-cavity coupling selected one nearest bending mode. Crossing
the midpoint between two modal frequencies abruptly changed the source mode
and its damping-derived coupling rate. This occurred in all three bars' treble
transition and around two fixed Kalimba box resonances.

The regression holds the bar pitch, velocity, hardness, contact position and
random seed constant, then changes only cavity tuning. It compares a 0.1-cent
interval with a 0.01-cent interval around five observed switch locations:
Rosewood MIDI 84, Padauk 89, Balafon 87, and Kalimba 57/68. This removes ordinary
phase movement of the pitched bar from the comparison.

| Metric, both 88.2 and 96 kHz internal rate | First round | Revised continuous coupling |
| --- | ---: | ---: |
| Reduction in difference energy when interval shrinks tenfold | 0–0.38 dB | About 20 dB |
| Fine-interval difference relative to the note | −67 to −46 dB | −128 to −97 dB |
| Continuity cases passing the unchanged new gate | 0/10 | 10/10 |

These were small deterministic timbral steps, not demonstrated loud clicks.
The most pronounced tested case was Padauk; there is no claim of a subjective
listening score. The new continuous projection shares the aperture between
bracketing modes instead of changing sources abruptly. Exact passivity is
tested separately in `CavityPhysicsTest`.

## Other bounded probes

- Body and Sympathetic response were tested on every model at 88.2, 96 and
  192 kHz internal rate. A raised-cosine mechanical gesture contains fixed
  85, 261.63, 523.25 and 1300 Hz components. This avoids sample-rate-dependent
  impulse area or random noise. The first-round maximum level difference was
  0.011 dB; broad spectral-fraction difference was under 0.073 dB. No rate
  defect was found. The new contract retains 0.5 dB limits for both measures.
- Buzz was disabled during a ringing note on all four models at MIDI 48/72.
  The probe first verifies that the membrane was active, then measures its
  isolated output after pressure drive is removed. By 20 ms after switching,
  the difference from the no-Buzz rendering rounded to zero in these probes.
  The contract requires at least 60 dB attenuation while the bar keeps ringing.
  No frozen or autonomous membrane tail was found.
- Single-cent master-tuning sweeps and fixed-pitch adjacent-register comparisons
  distinguished normal phase/modal-geometry changes from cavity-source changes.
  They did not identify another level discontinuity requiring a DSP change.
- The shared body/mesh blocks remain linear at fixed settings, so they do not
  generate new harmonics by themselves. The multisine test establishes their
  rate consistency; it is not an alias-rejection measurement of the complete
  nonlinear instrument.

## Causal radiation evidence

The final test compares two otherwise identical current voices, replacing only
the new modal radiation coefficients with identity readouts in one voice. Both
retain the current contact, continuous cavity coupling, random sequence and
pickup. Hard off-centre strokes at MIDI 48/60/72 are measured over 80 ms.

| Model | Close difference relative to full output | Far difference relative to full output |
| --- | ---: | ---: |
| Rosewood | −29 to −30 dB | −21 to −24 dB |
| Padauk | About −23 dB | −15 to −17 dB |
| Balafon | −29 to −31 dB | −21 to −25 dB |
| Kalimba | −51 to −56 dB | −46 to −51 dB |

Every model has a representative attack above the 0.3% RMS difference floor,
including after best constant-gain fitting. These are acoustic timbre changes,
rather than a global gain change. Piezo output, mechanical samples and stored
oscillator energy remain exactly identical at every rendered sample in all 12
paired cases. The helper's separate coefficient/directionality tests cover the
surface integral; this voice-level ablation establishes its signal-path effect.
The small Kalimba contribution is consistent with the compact tine surface and
the disclosed limited blend, rather than an equally strong effect forced onto
all four geometries.

## Reproduction

```sh
clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP \
  Tests/Round2ResponseTest.cpp DSP/*.cpp -o /tmp/maremba-round2-response
/tmp/maremba-round2-response
```

Before adding the radiation ablation, the continuity/rate/Buzz portion was
compiled against the frozen first-round implementation: exactly the ten new
continuity checks failed; shared-rate and Buzz-off checks passed. The final
test includes new radiation fields absent from that snapshot and is compiled
against current DSP. The revised implementation passes the complete focused
contract. `--measure` reports observations without enforcing acceptance gates.
