# Working conventions

## Architecture

This is Maremba's canonical repository: it owns the DSP engine in
`Source/DSP/` and the JUCE plug-in (VST3, AU, CLAP, Standalone) that wraps it.
The Reason Rack Extension in the private
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
- Front ends share `Source/DSP/MarembaControls.h` (the 48 controls, in Rack
  property order, and their engine mapping). Plug-in parameter IDs are those
  property names; `PLUGIN_MANUFACTURER_CODE Mrmb` / `PLUGIN_CODE Mrb1`,
  `BUNDLE_ID` and the CLAP id `cz.protocodus.maremba` never change: hosts
  recall sessions by them.
- `Source/PluginControls.h` and `Source/FactoryPresets.h` are generated from
  the Rack device by its `Tools/export_plugin_tables.py`; the Rack check fails
  when they and the factory patches differ. Change a preset there.
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

## Generated README parts

The demo table between `<!-- audio-demos-begin -->` and
`<!-- audio-demos-end -->` is written by `Tools/RenderDemos.cpp` from
`Docs/audio/midi/<NN>-<preset-slug>.mid` (one per factory preset; change a
source only on purpose, in its own commit). `main-build.yml` encodes the demos
to `Docs/audio/<NN>-<preset-slug>.mp3`, commits them with
`Docs/screenshots/maremba.png`, and rewrites the block between
`<!-- build-download-begin -->` and `<!-- build-download-end -->`. Never edit
these by hand. Version history is a ledger of what changed about the
instrument; pure refactors belong in git history.

## Build and verify

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DMAREMBA_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON -DMAREMBA_REQUIRE_PYTHON_TESTS=ON
cmake --build build-dsp --parallel 2
ctest --test-dir build-dsp -L unit --output-on-failure
```

Use a separate `build-plugin` directory (`-DMAREMBA_BUILD_PLUGIN=ON`, the
default) for the processor, editor and packaging; add `-DMAREMBA_BUILD_PLUGIN=OFF`
to `build-dsp`. Every test is `Maremba.<Suite>` with one tier label: `unit`,
`audio` (the preset demos), `render` (the editor screenshot) or `fuzz`. Select
the suites that cover a change with `-R`, then run the full suite before a
substantive DSP change is called done. Add a contract to `Tests/contracts.txt`
and it is built and registered automatically.

A test is never skipped, disabled or loosened to get green. Diagnose a failed
contract against the baseline before changing a fixture, and document why an
assumption was stale.
