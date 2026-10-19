# Maremba

Maremba is a physically modelled mallet-percussion instrument by Protocodus:
a concert rosewood marimba, a Guatemalan padauk marimba, a West African balafon
and a 17-key box kalimba, played through modelled close microphones, a room and
a contact pickup. It runs as a VST3, CLAP and Standalone plug-in on macOS,
Windows and Linux, plus an Audio Unit on macOS, and as a Reason Rack Extension,
both built from the JUCE-free C++17 engine in `Source/DSP/`.

![Maremba](Docs/screenshots/maremba.png)

## Audio demos

One demo per factory preset, rendered by every main build from
`Docs/audio/midi/` through the shipping engine.

<!-- audio-demos-begin -->

| # | Preset | Demo | Length | Peak |
| --- | --- | --- | --- | --- |
| 1 | Ambient Cathedral Bloom | [01-ambient-cathedral-bloom.mp3](Docs/audio/01-ambient-cathedral-bloom.mp3) | 20.0 s | -15.2 dBFS |
| 2 | Concert Grand Rosewood | [02-concert-grand-rosewood.mp3](Docs/audio/02-concert-grand-rosewood.mp3) | 20.0 s | -8.2 dBFS |
| 3 | Direct Piezo Mallet | [03-direct-piezo-mallet.mp3](Docs/audio/03-direct-piezo-mallet.mp3) | 18.3 s | -7.2 dBFS |
| 4 | Hard Acrylic Virtuoso | [04-hard-acrylic-virtuoso.mp3](Docs/audio/04-hard-acrylic-virtuoso.mp3) | 20.0 s | -10.4 dBFS |
| 5 | Rosewood Dampened Etude | [05-rosewood-dampened-etude.mp3](Docs/audio/05-rosewood-dampened-etude.mp3) | 18.6 s | -9.8 dBFS |
| 6 | Rosewood Roll Chorale | [06-rosewood-roll-chorale.mp3](Docs/audio/06-rosewood-roll-chorale.mp3) | 20.0 s | -11.7 dBFS |
| 7 | Soft Yarn Nocturne | [07-soft-yarn-nocturne.mp3](Docs/audio/07-soft-yarn-nocturne.mp3) | 20.0 s | -11.5 dBFS |
| 8 | Chiapas Folk Fiesta | [08-chiapas-folk-fiesta.mp3](Docs/audio/08-chiapas-folk-fiesta.mp3) | 18.8 s | -4.9 dBFS |
| 9 | Guatemalan Charleo | [09-guatemalan-charleo.mp3](Docs/audio/09-guatemalan-charleo.mp3) | 20.0 s | -6.5 dBFS |
| 10 | Mayan Dance Padauk | [10-mayan-dance-padauk.mp3](Docs/audio/10-mayan-dance-padauk.mp3) | 19.4 s | -5.9 dBFS |
| 11 | Ancestral Balafon | [11-ancestral-balafon.mp3](Docs/audio/11-ancestral-balafon.mp3) | 20.0 s | -6.8 dBFS |
| 12 | Birifor Gyil Buzz | [12-birifor-gyil-buzz.mp3](Docs/audio/12-birifor-gyil-buzz.mp3) | 20.0 s | -8.2 dBFS |
| 13 | Chopi Timbila Ensemble | [13-chopi-timbila-ensemble.mp3](Docs/audio/13-chopi-timbila-ensemble.mp3) | 20.0 s | -8.7 dBFS |
| 14 | Equipentatonic Log Keys | [14-equipentatonic-log-keys.mp3](Docs/audio/14-equipentatonic-log-keys.mp3) | 19.0 s | -7.2 dBFS |
| 15 | Mandinka Bala Heptatonic | [15-mandinka-bala-heptatonic.mp3](Docs/audio/15-mandinka-bala-heptatonic.mp3) | 20.0 s | -4.3 dBFS |
| 16 | Midnight Village Ritual | [16-midnight-village-ritual.mp3](Docs/audio/16-midnight-village-ritual.mp3) | 20.0 s | -9.9 dBFS |
| 17 | Sambla Talking Balafon | [17-sambla-talking-balafon.mp3](Docs/audio/17-sambla-talking-balafon.mp3) | 20.0 s | -6.0 dBFS |
| 18 | Kalimba Muted Thumbs | [18-kalimba-muted-thumbs.mp3](Docs/audio/18-kalimba-muted-thumbs.mp3) | 20.0 s | -9.2 dBFS |
| 19 | Kalimba Pick Sparkle | [19-kalimba-pick-sparkle.mp3](Docs/audio/19-kalimba-pick-sparkle.mp3) | 20.0 s | -8.3 dBFS |
| 20 | Kalimba Thumb Lullaby | [20-kalimba-thumb-lullaby.mp3](Docs/audio/20-kalimba-thumb-lullaby.mp3) | 20.0 s | -13.1 dBFS |

<!-- audio-demos-end -->

## Version history

### 1.5.1 — 2026-10-16 — the engine moves to its own repository

- `Source/DSP/` is the engine of the Rack Extension's 1.5.1f7 release candidate
  plus its following Main calibration (each model's recording gain set around a
  −7 dBFS phrase peak). The files are byte-identical to the Rack repository's,
  with their history carried over.
- The physics, behaviour and fuzz contracts and the table generators moved with
  it and run under CTest.
- First plug-in: VST3, AU, CLAP and Standalone, with the Rack Extension's 20
  factory patches as its presets and its controls as parameters (same names,
  same values, same sound). MIDI CC 1 is the mod wheel; the Close, Room and
  Piezo direct outputs are optional extra buses.

## Downloads

<!-- build-download-begin -->
**[Download main build 13](https://github.com/protocodus/virtual-instrument-maremba/actions/runs/39221588426/artifacts/12179581685)**, built from [`cb8b36cdb958`](https://github.com/protocodus/virtual-instrument-maremba/commit/cb8b36cdb958ddafe1eaee8f3733b29536904b19). It holds the macOS, Windows and Linux packages, `SHA256SUMS` and `BUILD.txt`.
<!-- build-download-end -->

---

## How it works

Every front end stores the same 48 controls (`Source/DSP/MarembaControls.h`)
and maps them to the engine the same way; the plug-in and the demo renderer
play MIDI through `Source/DSP/MarembaPlayer.h`. `MarembaEngine`
(`Source/DSP/MarembaEngine.h`) takes physical, already
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

- The plug-in has no sustain pedal and no MPE; the engine has neither.
- The render tier checks the screenshot's size and that it is not blank, not
  pixel identity with the committed image: the image is rendered on macOS, and
  fonts differ between platforms.
- Three table generators no longer reproduce the shipped tables, exactly as in
  the Rack repository before the move, so their checks are not registered in
  CTest. `generate_bar_modes.py` writes overtone ratios up to about 5% away
  from `BarModes.h`; `generate_kalimba_modes.py` looks for the 32-node
  radiation cache that `ModalRadiation.h` replaced with 64 nodes; and
  `generate_kalimba_shapes.py` differs from `KalimbaMountShapes.h` in the tenth
  significant digit. The shipped tables are what the contracts test; each
  check is registered again once its generator matches them.
- The modelling research, references and validation reports for this engine are
  still in the Rack repository's device documentation, which is private.
  Comments in `Source/DSP` that cite `docs/...` refer to those documents.

## Build

CMake 3.22+ and a C++20 compiler. Python 3 with NumPy, SciPy and mpmath adds
the generator and measurement checks. The plug-in build fetches JUCE 8.0.14 and
clap-juce-extensions at pinned commits (`MAREMBA_JUCE_PATH` and
`MAREMBA_CLAP_JUCE_EXTENSIONS_PATH` point at local checkouts instead); on Linux
it needs JUCE's usual ALSA, FreeType, fontconfig and X11 development packages.

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DMAREMBA_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build-dsp --parallel
ctest --test-dir build-dsp -L unit --output-on-failure    # also: audio, fuzz

cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-plugin --parallel
ctest --test-dir build-plugin -L 'unit|render' --output-on-failure
```

`scripts/build-macos.sh` builds the universal macOS plug-in, and the
`scripts/package-*` scripts write the distribution packages. Every test is a
`Maremba.<Suite>` CTest test with one tier label: `unit`, `audio` (every preset
demo renders, finite, below 0 dBFS and byte-identical on a second render),
`render` (the editor screenshot) or `fuzz` (the bounded parameter matrices and,
with `-DMAREMBA_ENABLE_SANITIZERS=ON` under Clang, a time-boxed libFuzzer run of
`Tests/DspFuzz.cpp`). `Tests/contracts.txt` lists the physics and behaviour
contracts. `EngineFuzzTest --extended` and `ExtremeRangeTest --full` run the
long matrices.

`Tools/Demos/` holds the offline research tools, which drive the engine directly
(they need clang++, and some need ffmpeg). `run_benchmark.py` renders the repertoire, touch and idiom studies
behind [`Docs/audio/demo-player.html`](Docs/audio/demo-player.html). The
`build_*_review.py` and `compare_*.py` scripts build A/B listening and review
pages. `fit_modal_reference.py` measures recordings such as the reference notes
in `Tools/Demos/Reference/`. The CPU and optimization fixtures sit in `Tools/`. The
MIDI, events and reference audio carry their own licences
([third-party notices](THIRD_PARTY_NOTICES.md#demo-and-reference-material)).

## Licensing

Maremba's source is available under the [PolyForm Noncommercial License
1.0.0](LICENSE): you may read, study, change and share it for any non-commercial
purpose, but not sell it or use it commercially. Official Protocodus builds
carry an additional permission in the same file to use them for any purpose,
including making music commercially. Commits made before this licence change
were published under MIT and remain so. The plug-in builds with JUCE (official
builds use a commercial JUCE 8 Starter licence) and clap-juce-extensions (MIT),
and the measured head responses are Apache-2.0; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
