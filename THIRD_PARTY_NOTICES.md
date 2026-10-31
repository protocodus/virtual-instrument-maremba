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

## Demo and reference material

None of this is part of the plug-in or the Rack Extension: the instrument is physically
modelled and ships no samples, MIDI or recordings, and the repository holds no
recordings (the soundfont reference notes were removed on 2026-10-31). It is demo and reference material for the tools in
`Tools/Demos/` and the rendered player `Docs/audio/demo-player.html`, kept here under the licences given below.

The compositions (Bach, Beethoven, Joplin, Mozart) are in the public domain. The
MIDI **sequences** of them are not: they are performances with their own
copyright and licence, and everything Maremba renders from them is an adaptation
of those sequences.

Hashes were taken on 2026-09-19. Every MIDI file was compared with the source
URL given for it on that day and matched byte for byte.

### Licences

- **CC BY-SA 3.0 DE**: Creative Commons Attribution-ShareAlike 3.0 Germany,
  <https://creativecommons.org/licenses/by-sa/3.0/de/>. piano-midi.de licenses
  Bernd Krueger's MIDI files under it (<http://www.piano-midi.de/copy.htm>) and
  asks for this attribution: Name: Bernd Krueger, Source: http://www.piano-midi.de.
- **CC BY-SA 4.0**: Creative Commons Attribution-ShareAlike 4.0 International,
  <https://creativecommons.org/licenses/by-sa/4.0/>. The IOCCC winner repository
  (<https://github.com/ioccc-src/winner>, file `LICENSE`) is published under it.

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
