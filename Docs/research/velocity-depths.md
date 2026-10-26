# Velocity depths

## Current calibration

The additive VOL calibration was introduced after the packaged 1.4.0f1 build.
Its original native test evidence is recorded in
[the calibration audit](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/velocity-calibration-2026-09-26.json).

The current source update after 1.4.0f3 selects **Sigmoid** as the initial device
response and in all 20 factory presets. This is the existing smoothstep curve,
`3u² − 2u³`, previously named Expressive; its stored index remains 3. Other saved
curve indices keep their meanings. The new default applies to the next plugin
build; historical release receipts describe their original defaults.

All ten small **VOL** knobs add velocity sensitivity to their associated
control: Hardness, Position, Decay, Carving, Coupling, Bloom, Sympathetic, Buzz,
Tension and Artifacts. Their native parameter names remain **VEL - …**.
Each depth is a fraction of the control's full knob range:

```
u = (MIDI velocity - 1) / 126
offset = depth * response(u)
effective normalized knob = min(1, base normalized knob + offset)
```

MIDI velocity 1 adds nothing. At velocity 127, VOL 0% adds nothing, 50% adds
half the full knob range and 100% adds the full range. The selected Soft,
Linear, Hard or Sigmoid response shapes the progression while preserving
those endpoints. This route uses no minimum-drive floor. The separate
physical excitation still responds to velocity when every VOL is zero.

The main knob is the starting value and the result is capped at its maximum.
For example, Hardness 20% with VOL 50% reaches 70% at velocity 127. Decay
adds the same normalized knob travel, then converts logarithmically: the
knob's minimum, midpoint and maximum are 0.1, 1 and 8 nominal units.
Starting at its natural midpoint, 10% VOL reaches about 1.52 at velocity
127; 50% reaches the maximum 8. A knob already at its maximum has no room
for an additional increase.

Kalimba now has a shorter sustain range: nominal Decay 0.1–1 is unchanged,
while 1–8 maps to 1x–2x natural sustain. This happens after adding VOL, so
velocity follows the same gentler response and cannot bypass the maximum.
The stored knob is normalized 0–1 and its nominal numeric display remains
0.1–8. Natural sustain now falls exactly at the knob's midpoint. Existing
user patches keep their stored positions and intentionally change under
this mapping; the factory bank migrates its base multipliers to preserve
each authored short, natural or extended setting.
The [Kalimba decay audit](../validation/kalimba-decay-2026-09-26.json) records the
before/after audio measurements and focused regression checks.

The engine evaluates each route at a new strike or restrike, after the
wrapper's base-control CV additions. Bloom and Sympathetic apply each
strike's effective setting to shared-resonance excitation. Buzz and Tension
retain the strike's additive offset when their main knobs move during its
ring. Other strike settings remain with that voice until a new strike.

Factory patches store Buzz and Buzz VOL at zero, so velocity cannot introduce
buzz. Eight other VOL depths remain at 10%. Decay VOL is zero in natural
presets; Ambient Cathedral Bloom, Midnight Village Ritual and Rosewood Roll
Chorale retain 10% for deliberate sustain extension. A user patch with Buzz
at zero and nonzero Buzz VOL can introduce buzz at higher velocities.
Device defaults are Hardness VOL at 20% and all other routes at zero, so
the default natural Decay does not lengthen automatically with velocity.

Older patches with nonzero VOL settings intentionally sound different under
this additive calibration; their stored knob values and property identities
are preserved. The historical multiplicative behavior and its validation
records below describe earlier builds and do not describe the current law.

## Historical implementation — 1.3.0b4

Three small brass knobs sit immediately below Hardness, Position and Decay,
aligned to their parent knob centres. Their 22 px brass bodies
distinguish them from the main 38 px stone knobs; blue pointers with dark outlines
make the positions readable. The printed captions say **VOL**, as requested;
their native popup/automation names are **VEL - Hardness**, **VEL - Position** and
**VEL - Decay**. All span 0–100%, default to 20%, and persist in patches and songs.
All 11 factory patches explicitly store 20% for each new control.

**1.4.0b1:** VEL - Position defaults to 0% and every factory patch stores 0% for it:
a player lands on the same spot at every dynamic, and with the physical mallet
contact the stroke's force already brightens a note (harder strokes compress the
head less long). Hardness and Decay keep their 20% defaults. Patches that store a
Position depth keep it.

These are positive velocity sensitivities. Let `v` be note velocity after the
selected Velocity Curve and `d` be depth divided by 100. Each route applies:

```
effective value = base knob value × (1 − d × (1 − v))
```

At 0%, the extra route is off. At 100%, the value follows velocity proportionally.
Full velocity always reaches the base knob setting. At the default 20%, a
mapped velocity of 0.5 uses 90% of the base setting. A base Hardness of 0.4 becomes
0.36, Position 0.5 becomes 0.45, and Decay 1× becomes 0.9×. The existing mapper's
1% physical-drive floor is included in `v`. This keeps the base
knobs useful as the values for the hardest strikes.

The existing physical model still responds naturally to touch even when these
extra depths are zero. Hardness and position retain the voice's physical limits;
decay remains at least 0.1×. Position modulation moves softer strikes toward the
lower end of its coordinate; spectral changes depend on the chosen bar/tine
position and need not be a monotonic brightness change.

The wrapper applies CV and mod-wheel additions to the base controls first. The
engine then evaluates the three routes at each strike using mapped MIDI or Gate
CV velocity. Each voice retains its own result. Automation changes affect new
strikes, including repeated notes and notes queued during a quality change; they
do not alter the decay or contact settings of an already ringing voice.

Properties are `velocityToHardness`, `velocityToPosition`, `velocityToDecay`;
normalized persisted defaults are 0.20, with new MIDI/automation IDs 286–288.
No existing identities or ranges changed. Old patches without these properties
receive the new 20% defaults, so their touch response intentionally changes.
Setting all three to zero restores the previous velocity-routing behavior.

The Striker radios form a horizontal row centred above the touch controls. Their
model-dependent labels retain Yarn / Cord / Rubber / Wood and Flesh / Thumb /
Nail / Pick. Space sits directly below ROOM volume, above the processing controls.
The rear layout is retained, with build 1.3.0b4 in the existing separated position.

Native contracts check the 20% defaults, zero-depth bypass, full-velocity identity,
independent destinations, velocity-curve ordering, audible soft-strike differences,
unchanged stored base knobs, unchanged ringing notes and updated restrikes. The
wrapper's mapping, sanitization, automation reachability and allocation checks
include all three controls. Focused acoustic tests disable the optional routes
to continue measuring the underlying model in isolation.

The 1.3.0b2 DSP validation artifacts and a pre-change snapshot are retained under
`Release/validation/velocity-depths-20260922/`. That build, preview and host
status are recorded in [the DSP validation record](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/velocity-depths.json).
The `local45 Deployment` build passed all eight suites and produced x86_64 and
arm64 libraries. All factory patches passed the wrapper render at 44.1/48/96 kHz.
Native default-2x performance remained about 76 µs median per 64-frame block at
48 kHz with 16 voices; the revised build had no wall/thread-CPU deadline misses
in any room. The before-change Chamber run had 11 wall-time misses with no
thread-CPU misses; that scheduling evidence is retained, not attributed to a DSP
fix. Native timing is separate from Recon or target-chip performance.

The 1.3.0b3 GUI refinement also passed all eight suites and both native builds.
Its payload and front/rear previews are retained under
`Release/validation/velocity-knobs-ui-20260922/`; see
[the GUI revision record](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/velocity-knobs-ui.json).
The current 1.3.0b4 layout and blue pointers passed all eight suites and were
installed with both native architectures. Current host-test status is recorded
in [the layout revision record](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/capture-layout.json).
Historical 1.3.0b1 audio and performance comparisons remain in
[Character and rooms](character-and-rooms.md).

## Historical 1.4.0: seven more routes

Carving, Coupling, Bloom, Sympathetic, Buzz, Tension and Artifacts gained the
same route (`velocityToCarving`, `velocityToCoupling`, `velocityToBloom`,
`velocityToSympathetic`, `velocityToBuzz`, `velocityToTension`,
`velocityToArtifacts`; MIDI/automation IDs 293–299, 0–100%, in the automation
group *Velocity Sensitivity* with the first three). The law is the one above,
evaluated at each strike on the mapped velocity:

- **Carving, Coupling, Artifacts** reach the strike directly: a soft strike is
  carved, coupled and disturbed as the scaled knob would be. On the Kalimba,
  Coupling also sets how strongly the plucked tine drives the box's air mode
  (the box's 0.75 law), so the strike's share of hole radiation follows it.
- **Bloom** and **Sympathetic** are shared resonances: the frame (or the
  Kalimba's box) and the ringing-along keys. Each strike drives them in
  proportion to its own scaled value, and the knob still sets their level, so
  one soft note in a chord blooms and sings along less than a loud one. The
  Kalimba's box runs its air and plate modes twice, on the plain and on the
  Bloom-weighted bridge force, to keep that per strike.
- **Buzz** and **Tension** stay live while a bar rings: moving the knob moves
  every ringing strike, each by its own factor.

A restrike takes its own velocity's values; a stolen or faded voice's routes
end with it. With every new depth at zero the engine plays as before these
controls existed, which is why they default to 0% (songs and patches saved
earlier load with 0%). Every factory patch stores 10% for all ten depths,
Hardness, Position and Decay included.

`Tests/VelocityRoutesTest.cpp` checks, on all four instruments, that a soft
strike with a route plays as the same strike with no route and the knob
scaled by its velocity (bit for bit where the knob reaches only the strike,
within 1e-4 RMS where it drives a shared resonance), that full velocity reaches
the knob at every depth, that Buzz and Tension keep each strike's factor when
the knobs move, and that a Bow stored by the 1.4.0b1 beta plays as the Shaft.
The wrapper contract maps and sanitises the seven properties and hears each
one change the sound.

## Current panel layout

All ten sensitivities are labeled **VOL** on the generated panel artwork; their
native parameter names remain **VEL - …**. These are velocity sensitivities,
not audio-level controls. Their 28 px widgets sit below their associated main
controls in the wider left column and compact Touch section. Four mutually
exclusive switches replace the Velocity Curve knob without changing its
property or saved indices: Soft (0), Linear (1), Hard (2), Sigmoid (3, formerly
Expressive). Sigmoid is the default for the device and every factory preset.
The small diagrams show the normalized response curves. VOL uses their full
0–1 endpoints; the separate physical excitation retains its minimum-drive
floor. Current factory defaults and route behavior are described in the
current calibration section above.
