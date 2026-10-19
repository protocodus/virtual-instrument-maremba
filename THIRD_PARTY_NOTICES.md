# Third-party notices

Maremba's original source code is covered by the project's `LICENSE` file.
The build downloads or consumes the separate dependencies below; they are not
relicensed by Maremba.

## JUCE 8.0.14

JUCE is Copyright (c) Raw Material Software Limited.

Repository: <https://github.com/juce-framework/JUCE/tree/8.0.14>

A copy of the JUCE licence text is vendored at
[`ThirdParty/JUCE-LICENSE.md`](ThirdParty/JUCE-LICENSE.md) so that it can be
included in every distributed bundle and installer package alongside Maremba's
own licence, as the packaging script does.

License: JUCE framework modules are dual-licensed under the GNU Affero General
Public License version 3 (AGPLv3) and the commercial JUCE licence. Building or
distributing Maremba with JUCE therefore requires either compliance with the
AGPLv3 for the complete combined work or an appropriate commercial JUCE
licence. Review the JUCE 8 licence terms before distribution:
<https://github.com/juce-framework/JUCE/blob/8.0.14/LICENSE.md>

Protocodus's official builds use JUCE under a commercial JUCE 8 Starter
licence. Maremba's own licence is not the AGPLv3, so anyone else who distributes
a build needs their own commercial JUCE licence.

JUCE includes or interfaces with additional third-party components. The
bundled components' licence texts are collected in
[`ThirdParty/JUCE-DEPENDENCY-NOTICES.txt`](ThirdParty/JUCE-DEPENDENCY-NOTICES.txt),
with upstream paths and hashes. This file accompanies each distribution.
It is the same file the other Protocodus instruments ship for this JUCE
revision; JUCE's own `LICENSE.md` indexes the sources. The VST3 SDK portions used through JUCE are identified
there as MIT-licensed; Apple's Audio Unit frameworks are supplied by the macOS
SDK and remain subject to Apple's terms.

## CLAP support

The CLAP plug-in uses the MIT-licensed `clap-juce-extensions` wrapper, pinned
to commit `c1a5ad025f95d01e03267857fa8276ebeed16500`:
<https://github.com/free-audio/clap-juce-extensions/tree/c1a5ad025f95d01e03267857fa8276ebeed16500>.
It is Copyright 2019-2020, Paul Walker. The complete upstream licence is
included in [`ThirdParty/CLAP-JUCE-EXTENSIONS-LICENSE.md`](ThirdParty/CLAP-JUCE-EXTENSIONS-LICENSE.md).

The wrapper brings in two dependencies at the revisions recorded by its
submodules:

- CLAP 1.2.7, commit `29ffcc273be7c7c651f6c9953b99e69700e2387a`:
  <https://github.com/free-audio/clap/tree/29ffcc273be7c7c651f6c9953b99e69700e2387a>.
  Its MIT licence is included in [`ThirdParty/CLAP-LICENSE.md`](ThirdParty/CLAP-LICENSE.md).
- CLAP helpers, commit `a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc`:
  <https://github.com/free-audio/clap-helpers/tree/a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc>.
  Its MIT licence is included in [`ThirdParty/CLAP-HELPERS-LICENSE.md`](ThirdParty/CLAP-HELPERS-LICENSE.md).

Both dependencies are Copyright (c) 2021 Alexandre BIQUE. These licence files
are included in every distribution archive and macOS installer package.

## SADIE II Database, D1 (KU100)

`Source/DSP/MeasuredCloseHrtf.h` holds head-related impulse responses modified
from the SADIE II D1 (KU100) measurements, Copyright 2018 University of York,
licensed under the Apache License, Version 2.0.

- Dataset: <https://doi.org/10.5281/zenodo.12092466>
- Licence: [`ThirdParty/SADIE-II-LICENSE.txt`](ThirdParty/SADIE-II-LICENSE.txt)
- Notice and description of the modifications:
  [`ThirdParty/SADIE-II-NOTICE.txt`](ThirdParty/SADIE-II-NOTICE.txt)
- Generation receipt (source hash, processing, error bounds):
  [`Tests/close-hrtf-generation.json`](Tests/close-hrtf-generation.json),
  checked by `Tests/generate_close_hrtf.py --check`

Every distributed build that contains the DSP must carry both files.

## Demo and reference material

None of this is part of the plug-in or the Rack Extension: the instrument is physically
modelled and ships no samples, MIDI or recordings. It is demo and reference material for the tools in
`Tools/Demos/` and the rendered player `Docs/audio/demo-player.html`, kept here under the licences given below.

The compositions (Bach, Beethoven, Joplin, Mozart) are in the public domain. The
MIDI **sequences** of them are not: they are performances with their own
copyright and licence, and everything Maremba renders from them is an adaptation
of those sequences.

Hashes were taken on 2026-09-19. Every MIDI file and reference sample was
compared with the source URL given for it on that day and matched byte for byte
(the reference WAVs after decoding the source MP3, as described below).

### Licences

- **CC BY-SA 3.0 DE**: Creative Commons Attribution-ShareAlike 3.0 Germany,
  <https://creativecommons.org/licenses/by-sa/3.0/de/>. piano-midi.de licenses
  Bernd Krueger's MIDI files under it (<http://www.piano-midi.de/copy.htm>) and
  asks for this attribution: Name: Bernd Krueger, Source: http://www.piano-midi.de.
- **CC BY-SA 4.0**: Creative Commons Attribution-ShareAlike 4.0 International,
  <https://creativecommons.org/licenses/by-sa/4.0/>. The IOCCC winner repository
  (<https://github.com/ioccc-src/winner>, file `LICENSE`) is published under it.
- **CC BY 3.0**: Creative Commons Attribution 3.0 United States,
  <https://creativecommons.org/licenses/by/3.0/us/>, the licence
  gleitz/midi-js-soundfonts gives its Fluid Soundfont renderings.
- **CC BY-SA 3.0**: Creative Commons Attribution-ShareAlike 3.0 Unported,
  <https://creativecommons.org/licenses/by-sa/3.0/>, the licence
  gleitz/midi-js-soundfonts gives its Musyng Kite renderings, and the licence of
  the Musyng Kite soundfont itself.
- **MIT**: the licence of the Fluid (R3) SoundFont itself; its notice is in
  [`ThirdParty/FLUID-R3-LICENSE.txt`](ThirdParty/FLUID-R3-LICENSE.txt).

### MIDI sequences: `Tools/Demos/Midi/`

`Tools/Demos/run_benchmark.py --download-missing` restores missing files from the URLs
below and verifies their recorded SHA-256 digests. Normal regeneration uses the
cached files without network access. The four piano-midi.de files reached us through the
reality3d/3d-piano-player repository; that repository's MIT licence covers its
own code, not Bernd Krueger's sequences.

- **`01_Bach_Prelude_C_Major.mid`** — J. S. Bach, Prelude and Fugue in C major, BWV 846.  
  Sequenced by: Bernd Krueger, piano-midi.de (file: "Copyright © 1996  Bernd Krueger.", "Copyright 2004 by Bernd Krüger.", "http://www.piano-midi.de").  
  Downloaded from: <https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/bach_846.mid>.  
  Licence: CC BY-SA 3.0 DE.  
  SHA-256: `107f2b6a298f283f8d26d4e8359669e7780f7ea64011b7844f2ed066d874a37e`
- **`02_Beethoven_Fur_Elise.mid`** — L. van Beethoven, Für Elise.  
  Sequenced by: Bernd Krueger, piano-midi.de (file: "Copyright © 2004 by Bernd Krueger", "http://www.piano-midi.de").  
  Downloaded from: <https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/for_elise_by_beethoven.mid>.  
  Licence: CC BY-SA 3.0 DE.  
  SHA-256: `a1481bdafe94d33e0de8934c97aec45534c908bc797bae7293f626074aec2e07`
- **`03_Joplin_The_Entertainer.mid`** — S. Joplin, The Entertainer.  
  Sequenced by: Sample MIDI file of the IOCCC 2020 'otterness' entry by Nathan Otterness (the file names no sequencer: only "The Entertainer", "Scott Joplin").  
  Downloaded from: <https://raw.githubusercontent.com/ioccc-src/winner/master/2020/otterness/entertainer.mid>.  
  Licence: CC BY-SA 4.0.  
  SHA-256: `780e68865a83a6649f9e7f5d002cd3b8700213a1fde32a9cac38c9242acf5f83`
- **`04_Beethoven_Moonlight_Sonata.mid`** — L. van Beethoven, Piano Sonata No. 14, Op. 27/2, 1st movement.  
  Sequenced by: Bernd Krueger, piano-midi.de (file: "Coypright Bernd Krueger http://www.piano-midi.de", "Copyright © 1998 by Bernd Krueger").  
  Downloaded from: <https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/mond_1.mid>.  
  Licence: CC BY-SA 3.0 DE.  
  SHA-256: `074a76e133bedf8005ff1e586beec9d4e97c013bcc72d56ff6df4e3c0d4e7c01`
- **`05_Mozart_Sonata_Facile.mid`** — W. A. Mozart, Piano Sonata No. 16, K. 545, 1st movement.  
  Sequenced by: Bernd Krueger, piano-midi.de (file: "Copyright © 2006 by Bernd Krueger", "http://www.piano-midi.de/").  
  Downloaded from: <https://raw.githubusercontent.com/reality3d/3d-piano-player/master/midi/mz_545_1.mid>.  
  Licence: CC BY-SA 3.0 DE.  
  SHA-256: `010bd27e25904822e04a51f207083722e469fccb0b446ce5545fbd37378268bf`

### Adaptations, released under the same licences

These are adaptations of the sequences above. Changes made: `run_benchmark.py`
keeps only note-on/note-off timing, pitch and velocity (tempo applied, every
other event dropped) to make the event streams, and Maremba then synthesises
audio from those streams. Protocodus releases each adaptation under the licence
of the sequence it was made from, with the attribution above.

#### Event streams: `Tools/Demos/Events/`

- `01_Bach_Prelude_C_Major.events` — from `01_Bach_Prelude_C_Major.mid`; CC BY-SA 3.0 DE; SHA-256 `b5a287a8d11883afa7ea7df9c85784e004119c96adc2c5ef03f9c8fe6f1f9f84`
- `02_Beethoven_Fur_Elise.events` — from `02_Beethoven_Fur_Elise.mid`; CC BY-SA 3.0 DE; SHA-256 `fc156b4ed5e292db3afcdb799c10fefd542351f4f39c3b8ba1065e6c377b3ebf`
- `03_Joplin_The_Entertainer.events` — from `03_Joplin_The_Entertainer.mid`; CC BY-SA 4.0; SHA-256 `660500fd90fa96ae0867b9d4ddb26f4634998d4e711f43cb89894264b19aa44a`
- `04_Beethoven_Moonlight_Sonata.events` — from `04_Beethoven_Moonlight_Sonata.mid`; CC BY-SA 3.0 DE; SHA-256 `1ee84494ca976feb630593c304e80de49d2d1b4c3bf9a6d47ff9bff15ce3ab17`
- `05_Mozart_Sonata_Facile.events` — from `05_Mozart_Sonata_Facile.mid`; CC BY-SA 3.0 DE; SHA-256 `0fa027b0726ecec7bb1643d460c990eb4bcdae0a4867b809a46b2757a65f6cd2`

`Tools/Demos/Events/test_c4.events` (one C4 note), `06_Position_Study.events`,
`07_Dynamics_Study.events`, `08_Pluck_Study.events` and `09_Repeated_Hits.events`
are Protocodus-authored diagnostic sequences, without third-party material. They
generate the position, dynamic, pluck and repeated-hit studies in the player.
The idiom studies `10_Roll_Chorale.events`, `11_Guatemalan_Redoble.events`,
`12_Bala_Ostinato.events`, `13_Amadinda_Interlock.events`,
`14_Kalimba_Hole_Wah.events` and `15_Dampened_Etude.events` are original
Protocodus phrases written by `run_benchmark.py` in the manner of the named
traditions; they quote no transcribed or recorded piece. Event times use nine
decimal places so the renderer can schedule the notes to the nearest audio
sample.

#### Embedded demo renders: `Docs/audio/demo-player.html`

The page embeds 46 AAC renders from the engine revision recorded in its manifest:
12 repertoire comparisons (8 from the Bach prelude and 4 from the Moonlight
Sonata) plus 28 original Protocodus touch studies and 6 original idiom studies.
The 12 repertoire renders are adaptations of Bernd Krueger's sequences and are
released under CC BY-SA 3.0 DE with the attribution above. The touch and idiom
studies are not adaptations of third-party sequences.

[`Docs/audio/demo-player-manifest.json`](Docs/audio/demo-player-manifest.json) records the SHA-256 of the
complete player and every decoded embedded AAC file, the WAVs they were
encoded from, the input event/MIDI files, and the DSP source files used for the
render. The `embedded_aac_files` list identifies the exact 46 entries included
in the committed player; the `renders` list carries their credits and licences.
The manifest is regenerated together with the player by
`python3 Tools/Demos/run_benchmark.py` so the attribution and hashes remain aligned.

The full 62-render set (20 model showcases, 8 striker comparisons, 28 touch
studies and 6 idiom studies) is written as WAV/AAC pairs into ignored
`Tools/Demos/Audio/`, with a local player. The 28 repertoire renders are adaptations of the respective
source sequences, under the same share-alike terms as those sequences;
anyone sharing them must preserve these credits and terms. Rendering includes
physical synthesis, a three-second decay tail in excerpts, a 10 ms endpoint
fade and safety attenuation only when needed to prevent export clipping.

### Measurements of the University of Iowa marimba recordings (1.4.0)

The seventh physical-model pass (the Rack device's `Docs/research/physical-models-round7.md`) compared the
Rosewood model with the University of Iowa Electronic Music Studios' Musical
Instrument Samples (MIS, 2013) of a concert marimba, which the Studios offer
for use without restrictions. The recordings were measured outside this
repository; no recording, excerpt or file derived from them is in the
repository or the product. These values were fitted to those measurements
(bloom time, rise, decay, beating and odd-harmonic levels, key by key): the
tube's in-situ Q factor `kTubeGapQ` (0.45) and Rosewood tube coupling (0.0071)
remain in `Source/DSP/MarimbaModel.h`. The historical Rosewood `airGain` of 10.4 and
rim-register constants `kRimHarmonic3` and `kRimHarmonic5Db` described the 1.4.0
radiation balance. Version 1.5.1f3 lowers that mouth readout and removes the
authored rim layer; the historical attribution
is retained here. The owner should confirm the MIS terms against the
distribution agreement's §2 before release, as for any source a value was fitted
to.

### Reference samples: `Tools/Demos/Reference/`

Eleven marimba notes used for listening comparison and as calibration
references. No synthesis code reads them and nothing of them is in the product.
`Tools/Demos/fit_modal_reference.py` measures them (`Tests/test_calibration_tools.py`
fits `MusyngKite_C4.wav` as a contract), and the `MusyngKite_*.wav.json` files
beside them are those measurements (partial frequencies, decay times, levels,
resonator bloom): data derived from the Musyng Kite renderings, shared under
the same CC BY-SA 3.0 terms with the attribution below. A listening page that
`Tools/Demos/build_listening_test.py` builds with any of these notes embeds them and
must carry the same attribution and share-alike terms. Each WAV is a byte-for-byte match of `afconvert -f WAVE -d LEI16 <note>.mp3`
(macOS, 16-bit PCM, 44.1 kHz, stereo) of the MP3 named below, from
[gleitz/midi-js-soundfonts](https://github.com/gleitz/midi-js-soundfonts), which
publishes pre-rendered notes of General MIDI soundfonts. They are unmodified
apart from that decoding.

- `FluidR3_*.wav`: rendered by gleitz/midi-js-soundfonts from the Fluid (R3)
  General MIDI SoundFont by Frank Wen (Copyright (c) 2000-2002, 2008 Frank Wen),
  published there under CC BY 3.0; the soundfont itself is under the MIT
  licence in [`ThirdParty/FLUID-R3-LICENSE.txt`](ThirdParty/FLUID-R3-LICENSE.txt).
- `MusyngKite_*.wav`: rendered by gleitz/midi-js-soundfonts from the Musyng Kite
  soundfont by Cose (Sarcyan), published there under CC BY-SA 3.0, which is
  also the soundfont's own licence. Redistributing its samples is subject to the
  share-alike condition.

- `FluidR3_C4.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/FluidR3_GM/marimba-mp3/C4.mp3> (MP3 SHA-256 `2f1382b9a79050de0be336fc53689ba246f5e516ce81c02f28a0e39e8d1e98f6`); WAV SHA-256 `aec4b86db99dcc8a3ab45c068596d3c6703c8aaf5174f8cfa14d856181a5e75f`
- `FluidR3_C5.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/FluidR3_GM/marimba-mp3/C5.mp3> (MP3 SHA-256 `7a38fcba7fb5ead5225e6322891b92c3ab3e2083997b78299f75b6f8b928ec2f`); WAV SHA-256 `d2d8f4f5e9f98a9261580a5c71bbb5c0ab2fbe176bcf8cf9a48b01ea6e9fe2fb`
- `MusyngKite_C3.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/C3.mp3> (MP3 SHA-256 `97dd9d46260a51a494459112911338a8767611f21eab7936b3035f41716548a5`); WAV SHA-256 `2ec47e02d9aefa7ded8c68d3642fbbe569f0d2545a3f9f379a40f5e17216527f`
- `MusyngKite_C4.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/C4.mp3> (MP3 SHA-256 `d26989b28e5b9582ce8971d7d009c43eff7b3d49b32d259aec3a412385f493fd`); WAV SHA-256 `2a929cb1d23c60caf89cb01227175530765f091bcc16d5c5013ccbf313016497`
- `MusyngKite_C5.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/C5.mp3> (MP3 SHA-256 `23a340b8a232f23a8f5655352b94c8c033df5e5b3282812ccc2f1f27c9e94c47`); WAV SHA-256 `39f3bca27c4d84b459d7a0f583d97562db1ad93a185d9ace5d8ed0f62f783bd7`
- `MusyngKite_C6.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/C6.mp3> (MP3 SHA-256 `c99aca24df0f4341145fc0cf40aa1a944ab9a217118d8de0f22cfeab63fc9a29`); WAV SHA-256 `3b744d8bd168fc4c6cb97c0fac70a06ffa4831763129c418b04ddde3a97ae3d3`
- `MusyngKite_E4.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/E4.mp3> (MP3 SHA-256 `72e6d65f287a25d9a8701f428ecae69fc487c965ce37f26c40e143dcd61557e3`); WAV SHA-256 `f63dbbaf9b2d5ab00d2c1f4b81d07e53dee17831f00f155eb4750693bc11ef4a`
- `MusyngKite_E5.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/E5.mp3> (MP3 SHA-256 `f9af578de00e96d24319295a5f32579e7166c9f4f7316a72e791c3db3f18b384`); WAV SHA-256 `28a40e6248be9429edd52dc20b097516b97424d9313b478aea31b4188659927c`
- `MusyngKite_G3.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/G3.mp3> (MP3 SHA-256 `6aa3350018fe0207d0a0f593ab575f32b694f41cd396f415744431422601d6a5`); WAV SHA-256 `735fed11e9efda3c09d0e32bc554792a619887d72006978710ded6daecca9a78`
- `MusyngKite_G4.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/G4.mp3> (MP3 SHA-256 `1c96f7894a213b48bf755e36afe29a8e5b3cf5152c127539358f7a40c61ebf08`); WAV SHA-256 `0235a111c85b1855a9e6e785b1c642bed9cda0cad7355721fd38355568cf1c54`
- `MusyngKite_G5.wav` — decoded from <https://gleitz.github.io/midi-js-soundfonts/MusyngKite/marimba-mp3/G5.mp3> (MP3 SHA-256 `cc70f00f19cad595d1f571cc2650e992e82cae6dfca7b0b8c35f726ca8952b49`); WAV SHA-256 `031b901df739a14cadf97b7f9140f455c1842f17cf5c1fafa365d4cdf8dae488`
