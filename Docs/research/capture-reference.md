# Maremba recording scene

This document describes the implemented capture revision after `d4437be`.
The [native validation record](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/capture-2026-09-27/qualified/README.md)
identifies the tested source revisions, corrected regressions and remaining
performance limits. The later 1.4.0f6 source includes these capture changes and
completed local45 and universal45 builds with SDK 5.0 on 2026-10-06. Interactive
Reason/Recon qualification remains unverified; the retained 1.4.0f3 package
predates these capture changes. See the [current status](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/README.md).
The later [room-motion checks](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/room-motion-2026-09-27/README.md)
separately validate the evolving diffuse tail described below. The f7
[room-quality update](reverb-quality.md) increases late-field density while
retaining this scene's direct paths, early reflections and microphone models.

The developing f3 source adds [Main-only pickup arrival alignment](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/capture-flight-2026-10-08/README.md).
Close remains an airborne-pressure receiver; Piezo remains a bonded-strain
sensor and charge amplifier. Their source models and polarity are unchanged.
The new routing reduces cancellation in the measured blend without replacing
either capture with a filtered copy of the other.

## Declared geometry

Coordinates are metres: X goes from bass to treble, Y away from the player, Z up.
The instrument centre is the local origin. The bar keyboard is C2–C7, in two
rows with actual generated bar widths and 5 mm gaps. Notes beyond that rack
transpose its end keys. The kalimba uses its seventeen alternating tine
positions, 8 mm apart. These are virtual reference layouts, not surveyed
specimens. Carving changes the structural modes within this fixed layout.

Close places a head at `(0,-0.45,0.40)`, facing the instrument along +Y. Its
left/right ears are at X=-/+0.09 m. Room uses two spaced pressure microphones
at `(-0.30,2.00,0.90)` and `(+0.30,2.00,0.90)`. Each channel hears every source.
The player hears low bars toward the left; pan coefficients no longer define
the recording geometry. The board, hole and frame have their own scene positions.
Board emission is currently collapsed to its centre, so the former voiced
Kalimba board-side pan is not part of the production microphone path.

Compact sources declare pressure at the 0.6 m Close reference distance. Room
uses a 1 m reference, so its input receives 0.6 times that pressure before
geometric attenuation. Frame and Kalimba board/hole sources use half their
former stereo-sum calibration per ear. Sympathetic sources retain their former
0.7 stereo-sum calibration, giving 0.35 per ear. These fixed conversions correct
an unintended doubling in both ears (and a 9.12 dB sympathetic gain error);
they do not normalize hits or change the stored physical resonance.

The reference Wood, Chamber and Hall are rectangular 6×4.5×3 m, 8×6×3.4 m and
18×12×7 m rooms. The instrument is placed at world coordinates `(3,1.7,1)`,
`(4,2.2,1)` and `(9,4,1)` respectively. Receiver poses relative to the instrument
stay fixed when the room changes. The speed of sound is 343 m/s.

## Close

Each struck bar's higher bending modes are projected separately at the two
receiver positions before capture. Their signed spatial cancellation and
relative propagation phase differ. Compact cavity/impact sources use a point
approximation; sympathetic keys, the frame, and the kalimba board/hole also
enter the same capture path. The fundamental retains the existing calibrated
dipole readout. This is not a full transient boundary-element radiation solver.

Head/ear filtering derives from the measured SADIE II D1 Neumann KU100 data,
release v2-2, DOI [10.5281/zenodo.12092466](https://doi.org/10.5281/zenodo.12092466).
The source dataset is diffuse-field equalized, low-frequency extended and
windowed, measured at 1.2 m. Its processed IR does not contain a raw 1.2 m
flight time. Minimum-phase filters and measured residual interaural delay are
separated; geometric common flight time and inverse-distance pressure gain are
applied once. The shorter reference scene is a distance adaptation of these
measurements, not a measured near-field KU100 response. Absolute SPL and raw
capsule sensitivity are not calibrated. License and modification notices are
retained in `Resources/Private/Licenses`.

Partitioned convolution uses the propagation interval as processing time. Its
partition and interpolation delays are subtracted from the explicit flight
delay, so a second unexplained processing delay is not added to the scene.
Frozen-data and full path transfer tests record approximation errors.

## Room

Each source has its own absolute direct and six first-order image paths.
Distances control arrival time and pressure attenuation. Each wall's absorption
controls both its reflection spectrum and the Eyring decay target of the diffuse
late field. The late network is a statistical approximation; higher-order
specular paths, exact room eigenmodes and scattering are not solved individually.
Room source emissions currently use a compact-source approximation, with the
existing calibrated bar/air balance. They are not a full directional surface
projection for every reflected ray.

The late field uses a twelve-line feedback delay network with three input
allpass diffusers. Small, slowly varying orthogonal rotations change how its
stored echoes scatter. Six reproducible pseudorandom walks follow smooth,
irregular transitions, bounded to approximately ±0.03 radians. Rotation
preserves the instantaneous energy of the feedback vector; the existing wall
absorption remains responsible for decay. This motion leaves direct and
first-order image paths, delay lengths and microphone responses unchanged. It is a subtle
statistical model of a changing diffuse field, not a simulation of moving walls
or air currents. It adds no noise signal and does not modulate delay taps.

The approach follows the principle of time-varying unitary feedback matrices
described by [Schlecht and Habets (2015)](https://doi.org/10.1121/1.4928394).
An explicit engine reset restores the pseudorandom sequence, preserving
repeatable renders. Clearing inaudible audio between phrases retains the
motion state; it pauses while the engine sleeps. The room's identity remains
stable while successive active passages encounter different late scattering.

From f7, Wood and Chamber use statistical loop paths 2.5 times their previous
lengths, increasing modal density. Hall retains its main loop lengths. Each
loop also has a short lossless allpass with fractional timing; its bounded
additional group delay is below 3.95% of that loop. Wall-dependent absorption
is fitted to the loop lengths and the existing decay target. Input diffusion,
the first late arrival and the original motion profile are retained. The
[production quality measurements](reverb-quality.md) cover spectral smoothness,
echo density, stereo compatibility and four-band decay at three sample rates.

The direct sound is delayed by its physical flight time. The early and late
parts are added before the Room coefficient. No separately voiced wet-gain
knob is hidden inside that coefficient. Identical wall-material filters operate
after summing the independently delayed paths: linear filtering commutes with
this sum. This reduces CPU cost without changing the calculated response.

Both Room channels then pass through the same declared pressure-condenser
capsule model: diaphragm inertia/compliance/damping, cavity pressure and vent,
capacitive input loading, buffer load and output coupling. Its fixed 1 kHz
sensitivity reference is shared by all rooms. It is a generic reference design,
not a measured replica or calibrated SPL measurement. The lumped acoustic
circuit follows [B&K's Microphone Handbook, volume 1, sections 2.3.10–11](https://www.bksv.com/doc/be1447.pdf).
The implementation lists its dimensions, component values and omitted effects
in `DSP/RoomMicrophone.h`.

The late feedback and diffusion loops preserve fractional delay time with a
causal allpass. Whole-sample rounding had moved coherent resonances between
sample rates: an isolated 1046.5 Hz fixture changed by -0.711 dB in the late
field at 48 kHz relative to 44.1 kHz. Fractional timing reduced that difference
to +0.0016 dB without changing the old standalone room implementation.

## Piezo

The bar readout now integrates signed surface strain over a fixed physical
electrode using the same Timoshenko finite-element modes and local thickness
that generate the sounding bar. Electrode lengths are 38/52/49 mm for the three
bar families, each 10 mm wide. These are declared design choices. One C4
calibration per family sets practical audio level. No commercial sensor
identity was present in the previous implementation.

Signed strain drives a declared 28 µm PVDF film, producing charge in coulombs
from its material constants and electrode area. The charge amplifier models
sensor/cable/input capacitance, leakage, feedback resistance/capacitance,
finite open-loop gain and bandwidth, slew rate, supply rails and overload
recovery. The reduced amplifier uses OPA1642 reference specifications; it is
not a transistor-level or measured hardware replica. A fixed digital level
calibration follows the reference voltage conversion, so physical charge and
amplifier headroom can be inspected independently of musical output level.

Kalimba uses one board-mounted film. Three displacement/velocity modes, with
declared plate mass and stiffness, respond to summed tine bridge force in
newtons. Electrode-averaged plate curvature determines strain. Per-tine nominal
pickup layers no longer bypass this shared sensor. Existing acoustic body modes
remain a voiced approximation; no claim of a fully coupled finite-element box
is made. See `Docs/research/piezo-reference.md` for component sources, calibration,
headroom measurements and explicit omissions.

## Main pickup arrival

An electrical signal arrives before sound reaches the Close microphones.
For Main, each bar's pickup contribution is independently decimated and delayed
by `distance(source, Close head centre) / 343 m/s` before summing. Kalimba's one
shared sensor uses the board-to-head distance, about 1.805 ms. Persistent key
identities preserve travelling samples through voice stealing and model changes.
The fractional interpolation consumes this physical flight; it does not add
another host-reported processing delay. Electrode polarity is unchanged.

Only the Main contribution uses this timing compensation. The raw Piezo socket
retains its electrical timing; Close and Room retain their acoustic propagation.
Consequently an external blend of the direct sockets has different timing from
Main. All direct sockets also bypass Drive and the Main mix controls. Their
fixed trims and independent overload protection remain separate: f3 deliberately
reduces the wooden direct-Piezo trim from .49 to .245 for headroom, without
changing the physical sensor or the Piezo contribution used by Main.

The [bounded arrival audit](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/capture-flight-2026-10-08/README.md) includes
all 128 notes on four models, 61 default chromatic Kalimba notes, and 915 wooden
fixtures spanning every MIDI note 36–96 with five Carving/Position combinations.
In the wooden sweep, cases losing more than 3 dB against Close alone fell from
21 to zero; cases losing more than 1 dB fell from 98 to 34. Median blend level
remained similar or improved. The default Kalimba sweep's worst loss improved
from 12.37 to 3.72 dB. These are 48 kHz/2x, velocity .65, 0.4–0.6 s measurements,
with optional variation routes and Main processing disabled, not universal
bounds for every control setting. Distinct pressure and strain responses still
interfere on some notes. No global polarity flip or fitted phase correction
was adopted. The alignment-only forty-case comparison preserves all five direct
channels byte-for-byte; that historical result precedes f3's separate trim change.

## Routing, rates and validation boundaries

Contact mechanics remain oversampled. Individual acoustic sources are
anti-alias decimated before the linear capture stage at host rate. In the
developing 1.5.0f3 source, Drive follows the complete Close/Room/Piezo Main mix
at host rate, using first-order antiderivative antialiasing to reduce generated
aliases. Its transfer is exactly linear through 10%, adding about 0.10 dB at
the default; higher settings progressively introduce rounded asymmetric soft
saturation. The antialiasing is not an alias-free guarantee. Warmth and musical
gain control follow Drive. Close and Piezo use linear coefficients; Room uses
the squared knob taper introduced in f2. There is no velocity normalization.
Direct sockets precede these coefficients and all bypass Drive.

The existing common two-millisecond output protection is separate from physical
propagation delays and the active Drive stage's frequency-dependent phase.
The focused f3 arrival contract verifies geometric phase, routing, reset,
travelling sound during actual slot reuse, and identical 1/7/64-frame batch
renders. Full f3 release qualification remains pending. Historical test results
below retain their pinned source scope.

Tests distinguish physical-state invariance, source/receiver geometry, measured
filter accuracy, circuit response, routing, reconstructed output headroom and
CPU deadlines. Native tests are not Reason/Recon or target-chip qualification.
The former main/direct comparison is corrected to include Main's documented
20 Hz DC blocker. A spaced microphone pair can have slightly negative
correlation; width tests now check unchanged Room output above natural width
and retain the mono-fold-down bound instead of requiring S/M ≤0 dB for every
phrase. The existing ±0.5 dB cross-rate level gate remains in force.

Tests of a source's mechanics retain their original anechoic reference. Tests
of delivered audio include flight time, measured ear filtering and coherent
interference between the tine, board and hole. This distinction matters for
stereo balance and strike variation: a narrowband tone can have the opposite
broadband level balance to its source position while retaining correct arrival
time differences. Complete-engine comparisons use independent transfer-function
oracles, and report cancellation-conditioned bins separately. Live controls
must still reach and silence their physical sources, while already emitted
sound finishes travelling through the recording scene.
