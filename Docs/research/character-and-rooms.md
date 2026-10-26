# Character and rooms — 1.3.0b1

**Historical design record:** the room algorithms below describe the former
standalone RoomBank. The shipping capture scene now uses RoomCapture; see
[the current scene](capture-reference.md) and [f7 room quality](reverb-quality.md).

This is an intentional sound update to the Rack DSP. It addresses weak character
controls, similar Rosewood/Padauk voices, room variety and the Striker selector.
It is not an upstream synchronization claim. Product identity, existing automation
IDs, stored parameter ranges and the default 2x quality remain unchanged.
Old songs load, but affected settings intentionally sound different.

The Artifacts description below records the earlier noise-led implementation.
The subsequent [mechanical realism update](mechanical-realism.md) replaces that
behavior with playing/contact events and a separate mounted Piezo model.

## Controls and voices

- **Artifacts** raises contact clack, friction and cord rattle, with a progressively
  stronger upper range. Position/hardness variation also increases; pitch jitter
  stays bounded at 0.65 cents. These are strike properties: turn the knob while
  playing new notes. A quiet touch does not acquire a constant noise floor.
- **Bloom** increases the shared frame resonance by 4.5x at the same setting.
  It is easiest to hear on low notes through Close/Room, and is absent from Piezo.
- **Buzz** adds an amplitude-dependent membrane rasp on all models, with a stronger
  Balafon response. It now follows the knob while notes ring. Zero is off.
- **Warmth** uses reciprocal low/high shelf gains spanning approximately ±6 dB,
  pivoting around 450 Hz, with 5 ms smoothing. It affects Main L/R only.
- **Padauk** now has a 1.25 s base decay (Rosewood: 3 s), broader/lighter soundbox
  resonance, stronger upper modes and a lower, woodier clack. These are musical
  voicing decisions, not species measurements. Rosewood's bar model is unchanged.
- **Striker** is four radio buttons: Yarn / Cord / Rubber / Wood. Kalimba changes
  the labels to Flesh / Thumb / Nail / Pick, preserving the existing value mapping.
- **Res Tune** has no front knob. Its old ±50-cent property and automation retain
  their DSP meaning for existing patches; Rear Setup displays the legacy value.
  This differs from the older Glide property, which remains inactive.

The updated factory bank uses Chamber for Mayan Dance Padauk and Chiapas Folk
Fiesta, and Hall for Ambient Cathedral Bloom and Midnight Village Ritual. Ambient's
width and level, and Mayan Dance's level, are rebalanced for the new voicing. The
default Concert Grand Rosewood patch retains Wood.

## Buzz range adjustment after release feedback

Buzz now spans 70% of the previous physical range: knob values 0%, 50% and 100%
drive the former 0%, 35% and 70% responses. The mapping happens once in the voice,
so new notes, restrikes and automation of ringing notes agree. It applies to the
three wooden instruments' membrane drive and slack limits, and to Kalimba's board
buzzer looseness. It does not simply reduce the buzz audio by 30%; the physical
snap and rattle thresholds respond to the reduced amount too. Zero remains off.

The property name, automation identity, stored 0–1 range and stored Buzz values
are unchanged. Existing patches and songs therefore load with the same displayed
settings but intentionally produce less Buzz; the former 70–100% physical range
is no longer reachable.

`BuzzRangeTest` pins the physical drive and slack limits and compares Kalimba's
buzzer audio with the reduced physical-kernel setting. It covers new notes,
restrikes, live changes and polyphony at 44.1/48/96 kHz. The existing membrane,
buzzer, live-audibility and pickup-isolation contracts retain their thresholds.

## Three room algorithms

`roomType` is a three-step persisted property: 0 Wood, 1 Chamber, 2 Hall. Missing
values in old patches default to Wood. The existing ROOM knob retains its routing
and gain law. Close and Piezo direct outputs remain independent of the room choice.

| Choice | Structure (1.3) | Character |
| --- | --- | --- |
| Wood | Existing stereo bank of four damped combs and two diffusers per side | Warm, colored reflections; original algorithm retained bit-for-bit |
| Chamber | Eight-delay Householder feedback network, input allpass, 4.5 kHz damping | Dense short decay; 0.85 s nominal low-frequency T60 |
| Hall | Four input allpasses and two cross-coupled allpass/delay tank branches, 3.6 kHz damping | Longer diffuse tail and broader space |

**1.4.0b1 rebuilt Wood and Chamber** (`DSP/AcousticRoom.h`, `DSP/RoomAlgorithms.h`,
checked by `Tests/RoomQualityTest.cpp`). The old Wood's combs left a sparse,
rough tail (normalized echo density 0.28–0.32 against the ≥ 0.8 of a dense room,
spectral roughness 12 dB). Wood is now a 6 × 4.5 × 3 m wooden room: the six
first-order image reflections for each microphone (4.2–13.6 ms), each fed from
its own channel, then a 12-line feedback-delay network (distinct prime lengths,
15–74 ms, three input allpasses, an orthogonal mixing matrix with no zero entry)
whose per-line shelving absorption gives 1.0 s at 125 Hz, 1.1 s at 500 Hz, 0.75 s
at 4 kHz and 0.47 s at 10 kHz. Chamber uses the same network with 12–64 ms lines
(0.85 s through the mids, 0.42 s at 4 kHz). Hall keeps its tank, taps and damping
and gains a 12 kHz input band limit. Each room runs at a 44.1–88.2 kHz tick with
its input band-limited and its wet output interpolated back (about 20 µs of
added wet latency; the dry path is unchanged), so the cost stays close to the
old rooms' in the Rack Extension build.

| Room (88.2–384 kHz) | T60 | 125 / 500 / 4k / 10k Hz | Echo density L / R | Roughness |
| --- | ---: | --- | --- | ---: |
| Wood | 0.98–0.99 s | 1.00 / 1.12 / 0.75 / 0.47 s | 0.89–0.96 | 6.3–6.6 dB |
| Chamber | 0.78 s | 0.87 / 0.85 / 0.42 / 0.23 s | 0.96–1.03 | 6.4–6.6 dB |
| Hall | 2.77–2.78 s | 3.20 / 3.27 / 2.07 / 1.29 s | 0.63–0.73 | 5.1–5.3 dB |

Levels match the 1.3 rooms within 0.2 dB. Because Wood now has reflections at
4 ms, the test that the room's bypass keeps the dry path continuous reads the
first 4 ms (`test_dsp`), and the transport-stop and pitch-bend tests measure the
instrument with the room off (a room keeps what was played, as a real room does).

The new code is an original implementation of standard structures. The Chamber
uses the orthogonal feedback-matrix and contracting loop-gain design described in
Julius O. Smith's [Feedback Delay Networks](https://www.dsprelated.com/freebooks/pasp/Feedback_Delay_Networks_FDN.html).
Hall is inspired by the two-branch tank topology in Jon Dattorro's 1997
[Effect Design, Part 1](https://freeverb3-vst.sourceforge.io/doc/EffectDesignPart1.pdf).
Our delay lengths, taps, damping and gains are original voicing choices; the Hall
uses fixed delays, not Dattorro's modulation or a claimed hardware emulation.

All delay memory is allocated during engine construction, including capacity for
8x operation. Changing room while audio is active clears the destination and
crossfades wet outputs over 50 ms; repeated requests are coalesced to the latest
choice after the active fade. An idle choice applies directly to the next strike.
Dry routing stays constant. The quiet-tail hold covers the Hall's longest delay.

## Reproducible audio comparison

`Demos/render_tone_review.cpp` renders the same four-note score at velocity 0.72,
44.1 kHz, default 2x, with deterministic resets. Controls are isolated and audio
is taken from Main L/R. `Demos/build_tone_review.py` builds a standalone player with
one common playback gain; clips are not individually normalized.

Evidence and the before-change DSP snapshot are under
`Release/validation/character-rooms-20260922/`. The player is `sound-review.html`;
`tone-review.json` records render hashes, parameters and measurements.

| Isolated control | Old endpoint difference, dB re low | Revised difference |
| --- | ---: | ---: |
| Artifacts | −29.93 | −21.18 |
| Bloom | −25.53 | −12.47 |
| Buzz | −34.83 | −12.82 |
| Warmth, cool → warm | −7.24 | +2.76 |

These are difference-signal energy ratios, not perceptual loudness scores. For the
first note, Padauk's 200–500 ms RMS relative to the first 100 ms falls from −9.88
to −14.88 dB. Rosewood remains −6.86 dB. The tests also require shorter relative
Padauk decay at MIDI notes 48, 60 and 72.

## Validation scope

New native contracts cover medium-velocity control differences, live Buzz, Warmth
shelf range and automation smoothing, model decay separation, and room impulse
responses at 44.1/48/96 kHz. Room checks require finite stereo tails, repeatable
reset, distinct responses, legacy Wood identity, idle selection, allocation-free
live changes and eventual engine silence. Wrapper contracts cover room value
mapping, sanitization and changes during ringing notes. Both GUI trees validate
radio indices, dynamic Striker labels and front automation coverage.

The native performance probe (`Demos/performance_review.cpp`) uses 48 kHz, 64-frame
blocks, 16 voices, default 2x and 4,096 measured blocks with periodic restrikes.
The initial paired run measured median wall time of 61.7 µs before and 75.5 µs
after for Wood; Chamber and Hall were 74.2 and 75.0 µs. All three revised modes
had zero wall/thread-CPU deadline misses against the 1,333 µs deadline. The
increased cost includes the newly audible, active default Buzz path. These are
native engine timings, not Reason scheduler or target-chip CPU claims.

Historical 1.2.2f1 Recon evidence in `recon-startup-validation.json` does not prove
this build. Current build/host results are recorded separately after validation.
