# Working conventions

## Architecture

This is Maremba's canonical repository: it owns the DSP engine in
`Source/DSP/`. The Reason Rack Extension in the private
`protocodus/reason-rack-extensions` repository is a thin adapter that mounts
this repository as the pinned submodule `Examples/Maremba/Instrument` and
compiles `Source/DSP/*.cpp` directly. DSP and playing-model changes land here
first; the Rack pin then advances. Reason property mapping, MIDI/CV, routing,
host latency, patches and panels stay in the adapter.

Repository layout, naming, CI, versions and the `Docs/` taxonomy follow the
Protocodus portfolio standard kept in the project, not in this repository.

- The DSP version is the product version: `project(Maremba VERSION X.Y.Z)` in
  `CMakeLists.txt`. Any DSP or behaviour change bumps it; the Rack device's
  `version_number` takes the same `X.Y.Z` plus its own stage suffix.
- `Source/DSP/` is JUCE-free and C++17 (the Rack toolchain). No allocation,
  file I/O, locks or exceptions on the audio thread.
- This repository is public. Never add Reason/Jukebox SDK material, secrets or
  unreleased commercial material.

## Generated tables

`BarModes.h`, `KalimbaModes.h`, `KalimbaMountShapes.h`, `ModalRadiation.h`'s
kalimba cache, `OutputPeakKernel.h` and `MeasuredCloseHrtf.h` are generated.
Change them only through their `Tests/generate_*.py` script, and keep each
registered check passing. Three generators no longer match their shipped
tables (README, Known gaps): bring a generator back to its table before using
it to change one. `MeasuredCloseHrtf.h` is derived from SADIE II
(Apache-2.0); its receipt under `Docs/validation/` must stay consistent.

## Build and verify

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DMAREMBA_REQUIRE_PYTHON_TESTS=ON
cmake --build build-dsp --parallel 2
ctest --test-dir build-dsp -L unit --output-on-failure
```

Every test is `Maremba.<Suite>` with one tier label (`unit` or `fuzz`). Select
the suites that cover a change with `-R`, then run the full suite before a
substantive DSP change is called done. Add a contract to `Tests/contracts.txt`
and it is built and registered automatically.

A test is never skipped, disabled or loosened to get green. Diagnose a failed
contract against the baseline before changing a fixture, and document why an
assumption was stale.
