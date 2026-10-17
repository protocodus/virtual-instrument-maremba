# Maremba

Maremba is a physically modelled mallet-percussion instrument by Protocodus:
a concert rosewood marimba, a Guatemalan padauk marimba, a West African balafon
and a 17-key box kalimba, played through modelled close microphones, a room and
a contact pickup. This repository owns its DSP engine, a JUCE-free C++17 library
in `Source/DSP/`. The Reason Rack Extension builds this engine through a thin
adapter; a VST3/AU/CLAP/Standalone plug-in is not built yet.

## Audio demos

<!-- audio-demos-begin -->
No preset demos are rendered from this repository yet.
<!-- audio-demos-end -->

## Version history

### 1.5.1 — 2026-10-16 — the engine moves to its own repository

- `Source/DSP/` is the engine of the Rack Extension's 1.5.1f7 release candidate
  plus its following Main calibration (each model's recording gain set around a
  −7 dBFS phrase peak). The files are byte-identical to the Rack repository's,
  with their history carried over.
- The physics, behaviour and fuzz contracts and the table generators moved with
  it and run under CTest.

## Downloads

<!-- build-download-begin -->
No builds yet: this repository has no plug-in target.
<!-- build-download-end -->

---

## How it works

`MarembaEngine` (`Source/DSP/MarembaEngine.h`) takes physical, already
de-normalised `EngineParameters`, MIDI-style note events, pitch bend and mod
wheel, and renders a stereo Main pair plus five direct outputs (Close L/R,
Room L/R, Piezo). Every field is clamped again inside the engine, so any finite
value is safe. It runs internally at 2×, 4× or 8× the host rate.

- **Bars and tines** (`MarimbaModel.h`, `BarModes.h`, `KalimbaModes.h`,
  `KalimbaMountShapes.h`): per-key modal banks from Timoshenko finite-element
  bars, carved to each instrument's practice (concert 1:4:10 rosewood, padauk
  tuned to its fundamental, balafon keys toward their own overtone ratios), and
  kalimba tines on a yielding bridge fitted to Chapman's measured overtone
  ratios. `Tests/generate_bar_modes.py` and the `generate_kalimba_*` scripts
  generated these tables.
- **Contact** (`MalletContact.h`, `ContactExcitation.h`): a mallet head as a mass
  on a nonlinear, hysteretic Hertzian spring, solved each sample against the
  bar's driving-point response, so contact time, force and spectrum follow from
  mallet and stroke; restrikes meet the bar mid-swing.
- **Resonators and buzz** (`MarembaVoice.cpp`, `SlackMembrane.h`,
  `BuzzerRattle.h`): one air mode per tube, box or gourd exchanging energy with
  the bar through an energy-preserving junction; slack mirliton and charleo
  membranes, and kalimba board buzzers that lift off above their preload.
- **Shared body** (`SympatheticMesh.h`, `FrameBody.h`, `KalimbaBody.h`):
  sympathetic ringing of unstruck keys, model-specific frame modes and the
  kalimba's shared box with its air resonance and hand-covered sound hole.
- **Capture** (`CaptureScene.h`, `CloseCapture.h`, `RoomCapture.h`,
  `PiezoPickup.h`): close capture through measured KU100 head-related responses
  (SADIE II, see `THIRD_PARTY_NOTICES.md`), a room with geometric early paths and
  diffuse fields (Wood, Chamber, Hall), and a strain-reading piezo with its
  mounting and input circuit.
- **Output** (`AnalogPreamp.h`, `AnalogLimiter.h`, `OutputHeadroom.h`,
  `MainCalibration.h`): Main drive with antiderivative antialiasing, Warmth tilt,
  calibrated Volume, an optional analog-modelled limiter and a 2 ms look-ahead
  headroom control on every output. Direct outputs bypass the Main stages.

## Known gaps

- No JUCE plug-in, editor screenshot, factory preset bank or preset demos yet,
  so the `audio` and `render` test tiers are empty.
- Three table generators no longer reproduce the shipped tables, exactly as in
  the Rack repository before the move, so their checks are not registered in
  CTest. `generate_bar_modes.py` writes overtone ratios up to about 5% away
  from `BarModes.h`; `generate_kalimba_modes.py` looks for the 32-node
  radiation cache that `ModalRadiation.h` replaced with 64 nodes; and
  `generate_kalimba_shapes.py` differs from `KalimbaMountShapes.h` in the tenth
  significant digit. The shipped tables are what the contracts test; each
  check is registered again once its generator matches them.
- CI builds and tests on Linux and macOS with Clang, as the Rack Extension
  compiles the engine. MSVC does not build it yet: it rejects an uncaptured
  `constexpr` local in `ModalRadiation.h`'s `BarDirectRadiationScale`, and
  `Tests/test_dsp.cpp` uses POSIX timing and allocation. A Windows plug-in
  needs both fixed first.
- The modelling research, references and validation reports for this engine are
  still in the Rack repository's device documentation, which is private.
  Comments in `Source/DSP` that cite `docs/...` refer to those documents.

## Build

CMake 3.22+ and a C++17/C++20 compiler. Python 3 with NumPy, SciPy and mpmath
adds the generator and measurement checks.

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-dsp --parallel
ctest --test-dir build-dsp -L unit --output-on-failure
ctest --test-dir build-dsp -L fuzz --output-on-failure
```

`Tests/contracts.txt` lists the physics and behaviour contracts; each becomes a
`Maremba.<Suite>` CTest test. `EngineFuzzTest --extended` and
`ExtremeRangeTest --full` run the long matrices.

## Licensing

MIT, see [`LICENSE`](LICENSE). The measured head responses are Apache-2.0, see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
