# Physical-model review — 14 October 2026

This review starts at local `089d7c05`, whose Maremba subtree matches published
`4905d978`. It covers Imperial Rosewood, Mayan Padauk, Balafon Ancestral and
Kalimba Artisan, plus the shared mechanical and capture paths. Earlier physical
model rounds are context, not proof that the current implementation passes.

At the user's subsequent request, Invert Piezo and Stereo Spread move together
from Output into Capture. Variance retains its new VOL depth. The layout update
does not change those properties, their mappings or defaults.

## Changes

The mallet's fibre-slip modulation now participates in the implicit contact
solve. Previously it multiplied the force *after* the solver had found the
compression. The force therefore described a different compression from the
one used to calculate it. A constant texture factor must give the same force
history as an independently configured head with proportionally scaled shell
and core stiffness. The old implementation fails that comparison by 0.135%
in the first 88.2 kHz fixture; the corrected implementation agrees within
floating-point rounding across both textured head types, both modulation signs
and 88.2–1536 kHz. This is a small numerical correction, not a new measured
model of yarn friction. Untextured contact retains its existing response.

Loose Kalimba buzzers retain their restitution rebound instead of being pinned
back to the board on the next sample. Ballistic landing times are solved within
the sample, and the diminishing final bounces settle with bounded work. Simply
retaining velocity in the former Euler/projection solver would cause persistent
one-sample chatter. This intentionally increases some high-Buzz responses:
the measured voice matrix reaches +5.08 dB RMS and +6.73 dB peak. All 48 measured
default-Buzz Kalimba cases are unchanged; no compensating gain was added.

The Balafon membrane-edge burst now decays in physical time. Its former fixed
per-sample pole made the tail shorter as sample rate or Quality increased.
The 88.2 kHz reference decay is preserved at other internal rates.

Kalimba hole-cover onset now transitions continuously into the empirical
end-correction approximation. The former branch produced a 14.61-cent resonance
jump and 3.31% radiation jump for an infinitesimal hand movement. A narrow
smoothstep joins the approximation's onset and existing valid branch; values
outside that transition remain unchanged. This interpolation is not a new
measured hand model.

The [review evidence](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/physical-review-2026-10-14/README.md) records
independent failure witnesses, final verification and retained failed attempts.

## Coverage and retained assumptions

| Path | Review focus | Interpretation |
| --- | --- | --- |
| Rosewood bars | Timoshenko modes, mass/contact geometry, carving, nodal supports, radiation, in-situ tube tuning and loss | Mode shapes are generated from declared geometry; register/radiation and mallet calibration still contain voiced assumptions. |
| Padauk bars and boxes | Distinct modal ratios/losses, active cavity mode, charleo register law, contact and radiation | Shares numerical machinery with Rosewood but retains different geometry, coupling and excitation. The treble charleo exponent remains a voiced extrapolation. |
| Balafon bars, gourds and mirlitons | Bar/cavity exchange, slack membrane tension and loss, edge bursts, tuning and sympathetic key selection | Membranes are reduced nonlinear oscillators; edge noise and relative membrane strengths are voiced. This is not a measured full gourd/membrane reconstruction. |
| Kalimba | Mounted tine shapes, bridge reaction, pluck release/preload, shared plate/hole, cover, board strain pickup and loose buzzers | Steel tine excitation and shared body remain distinct from struck wooden bars. Mount interpolation and the one-way body model have stated limits. |
| Contact and damping | Implicit nonlinear head force, core engagement, fingertip/dead stroke, rate scaling and texture | Passive-contact and release contracts supplement the force-law consistency regression. Random texture is an excitation approximation. |
| Shared supports | Frame/body modes and sympathetic receivers, routing, real key selection and decay | These are reduced, partly one-way transmission models, not a fully reciprocal instrument frame. |
| Close capture | Per-key geometry, directional modal pressure, measured HRTF, flight time, oversampling and persistent source tails | A declared virtual listening scene with a fixed listener; no individualized head or near-field HRTF measurement is implied. |
| Room capture | Image paths, absorption-derived decay, diffuse feedback network, scattering and microphone circuit | Geometry and first reflections are explicit; the late field is statistical. Wall coefficients are declared/voiced, not a measured venue. |
| Piezo and Main | Signed bar/board strain, PVDF charge, amplifier state, source-specific Main alignment, inversion and optional spread | Inversion still acts on the electrical pickup. Stereo Spread adds a small optional side component after the natural capture mix. Instrument-specific cancellation remains expected. |

The obsolete nominal pickup helper and legacy RoomBank are distinguished from
the production surface-strain pickup and RoomCapture paths. Their unit tests
alone do not establish current capture behavior.

## Follow-up work requiring a larger model change

Kalimba buzzer sound currently reaches the acoustic paths without a calibrated
reaction force into the shared board. Consequently the board-strain pickup
does not sense that extra landing motion. The existing buzzer impulse is in
normalized velocity units; feeding it directly into an SI strain sensor would
be dimensionally wrong. A defensible extension needs a declared buzzer mass,
contact location and force projection into the board modes. The current
limitation is retained explicitly instead of reviving the former synthetic
pickup mix.

Geometry changes on an already ringing key also warrant a separate transition
design: Carving changes the modal mass, contact and radiation readout used for
the retained ring. This is different from an ordinary same-geometry restrike.
The wooden-model probe records its severity and the conditions that expose it;
a geometry/state transition should be designed together rather than hiding it
with a level adjustment.

Historical six-song paired renders remain tied to their original source
snapshot. This source review does not rewrite those files or claim a new
installed SDK binary, U45 package, host audition or portal delivery.
