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

## Measured response and limits

The data is [SADIE II v2-2, D1 KU100](https://doi.org/10.5281/zenodo.12092466),
with 8,802 measured directions. The selected SOFA file contains 48 kHz,
256-sample responses measured at 1.2 metres. The file explicitly identifies
low-frequency extension, diffuse-field equalization and approximate linear
phase. Consequently, its shared processing delay is not interpreted as a raw
1.2-metre flight time, and its response is not described as an unequalized
capsule response. KU100 uses two pressure microphone capsules in artificial
ears; the model does not add a directional-microphone proximity boost.
See the [Neumann product description](https://www.neumann.com/en-ee/products/microphones/ku-100).

For each enclosing measured direction the generator reconstructs the
minimum-phase response. It interpolates those phase-aligned impulse responses
using spherical-triangle weights and retains 160 taps at 48 kHz. A separately
fitted 200–2,000 Hz interaural excess-phase slope preserves residual ITD; fitting
the two ears together cancels common processing delay. Additional all-pass
phase is not retained. The fixed scene has 203 unique profiles shared by its
515 source identities. No extra invented pinna or capsule filter is applied.

The measured far-field directional response is adapted to the closer virtual
scene through its common distance and propagation. This is a generic binaural
approximation: distance-dependent changes to the measured head diffraction,
individual ear anatomy, complete finite-distance wavefront scattering and an
exact hardware replica are not claimed. Extended bars also use a centre-angle
HRTF after their receiver-specific signed surface projection, rather than an
HRTF for every surface quadrature point.

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

## Reproduction and redistribution

Download the D1 HRIR SOFA archive from the pinned dataset record, extract
`D1_48K_24bit_256tap_FIR_SOFA.sofa`, then run:

```sh
python3 Tests/generate_close_hrtf.py --sofa /path/to/D1_48K_24bit_256tap_FIR_SOFA.sofa
```

Generation requires NumPy, SciPy and h5py; production has none of these
dependencies. The script checks the source SHA-256, generates the actual C++
scene positions, records source/dependency hashes, and publishes the generated
header atomically. The receipt distinguishes finite-tap error from the separate
angular and near-field modeling approximations.

The data is Apache-2.0 licensed. Redistributed products containing it must
include [the license](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Resources/Private/Licenses/SADIE-II-LICENSE.txt) and
[the attribution/modification notice](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Resources/Private/Licenses/SADIE-II-NOTICE.txt). The source
SOFA file is not needed at runtime. The generator copies both notices into
`Resources/Private/Licenses` for the SDK packer. Run
`python3 Tests/generate_close_hrtf.py --check` to verify frozen hashes and exact
equality of canonical and packaged notices, without scientific Python packages
or the original SOFA file.
