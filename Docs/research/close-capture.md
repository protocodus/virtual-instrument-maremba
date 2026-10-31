# Close binaural capture

Close records each acoustic source through two independently projected receiver
fields and a measured head/ear response. It includes every played key, individual
sympathetic keys, and the separately positioned frame, kalimba board and hole.
The source pressure is a voiced physical-model readout, not calibrated absolute
SPL. Close, Room and Piezo remain independent capture paths.

The coordinate system uses X from bass to treble, Y toward the audience, and Z
up. The listener's head is at `(0, -0.45, 0.40)` metres, facing +Y. The left and
right ear positions are X = −0.09 and +0.09 metres. Key positions come from the
instrument geometry, and all room types use the same receiver positions relative
to the instrument. See [CaptureGeometry.h](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/DSP/CaptureGeometry.h).

Each receiver's modal field removes its own source-centre propagation phase.
The renderer therefore adds source-to-head-centre flight time and the measured
residual interaural delay exactly once. It applies the common relative pressure
factor `0.6 m / source-to-head-centre distance`. It does not add geometric
ear-to-ear delay again, an arbitrary stereo pan, or artificial crossfeed between
the already separate receiver fields.

## Head response and limits

Since 1.6.0 the head is synthesized, not measured. Until then it was SADIE II
v2-2, D1 KU100 (measured dummy-head impulse responses); the 2026-10-31 audit
that every Protocodus instrument plays only synthesized sound replaced it.

The listener's head is a rigid sphere of radius 0.09 m, the distance of each
`CloseEar` from `CloseHeadCentre` in `CaptureGeometry.h`, so the ears lie on
the interaural axis. For each source the generator evaluates the exact series
solution for a point source at that source's own distance from a rigid sphere
(Duda and Martens, "Range dependence of the response of a spherical head
model", JASA 104(5), 1998, eq. 4), relative to the free field at the head
centre: near-field level and head-shadow changes follow from the scene rather
than from a far-field measurement adapted to it.

Each ear is divided by its diffuse-field response, the sphere's power average
over 2,048 evenly spread directions at 1.2 m, so the head shapes direction
rather than overall tone, and one broadband trim of −3.64 dB keeps the mean
of MainCalibrationTest's four Init anchors where the measured set had pinned
them (the factory Volumes then re-trimmed eight Close-heavy patches). Each ear is then
faded out over 20–24 kHz (a raised cosine to −60 dB: the 48 kHz table
cannot hold the head above its Nyquist, and a sphere is still loud there, so
an unfaded edge would ring once the kernels are resampled for a 96 kHz host),
made minimum phase and truncated to 160 taps at 48 kHz; a separately fitted
200–2,000 Hz interaural excess-phase slope gives the residual ITD, which
agrees with Woodworth's formula within the fit band. The 203 unique profiles
are shared by the 515 source identities.

A sphere has no pinnae, neck or torso: there are no elevation or front/back
cues beyond the head's own shadow. Extended bars use a centre-angle response
after their receiver-specific signed surface projection, rather than one for
every surface quadrature point.

## Runtime and validation

The portable renderer works at native host sample rate after source-specific
anti-alias decimation. Windowed-sinc resampling prepares the frozen kernels at
setup. Eight-tap fractional delays place the nominal arrivals between samples.
As with any finite band-limited interpolator, a short pre-ring can surround
that nominal arrival; the module itself is causal and does not read future
input. Filter resampling padding and FFT partition delay are deducted from the
propagation line. They do not add extra device latency.

Partition size follows the nearest source's physical travel budget. Staggered
groups spread FFT work across host callbacks. Only active sources and their
complete finite tails enter convolution; null inputs, voice releases and lower
input counts do not discard a wave already travelling to the microphones.
Storage is allocated at setup, with no allocation in sample processing.

[CloseCaptureTest.cpp](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Tests/CloseCaptureTest.cpp) checks all 515 source
configurations at eight rates from 8 to 192 kHz. Its frequency-domain oracle
compares the rendered impulse with the frozen response, geometric flight and
residual ITD independently of the convolution implementation. It also tests
superposition, ear isolation, arbitrary activation phase, full tail drainage,
Reset replay, rate changes, finite-state handling and subnormal inputs.
The microbenchmark is a component measurement, not an entire-engine realtime
guarantee; see the accompanying validation receipt for observed misses.

## Reproduction

```sh
python3 Tests/generate_close_hrtf.py
```

Generation needs NumPy and SciPy; production has no such dependency. The
script compiles the actual C++ scene positions, records geometry hashes and
publishes the generated header atomically. `python3 Tests/generate_close_hrtf.py
--check` (the `Maremba.CloseHrtf` test) verifies the frozen hashes without
scientific Python packages.
