# Maremba demo rendering and audio verification

- 62 stereo 44.1 kHz / 16-bit WAVs and 192 kbit/s AAC files regenerated from current DSP.
- 1499.3 seconds of audio; 74.57 seconds of native engine rendering (20.1× aggregate real-time).
- All source samples finite; all WAVs non-silent with zero clipped PCM samples; all decoded AACs finite with peaks below 0 dBFS.
- Maximum decoded AAC peak: -1.23 dBFS.
- 64-frame host blocks split at exact event frames, 24 voices; oversampling: 2x Studio.
- Repertoire excerpts reserve the final three seconds for acoustic decay; a 10 ms endpoint fade prevents truncated tails clicking.
- No loudness normalization or mastering compression. Safety attenuation, if needed, is disclosed per render in the manifest.
- Every demo preset uses Buzz 0% and Drive 1%; actual values are recorded per render in the manifest.
- 6 original idiom studies play the engine's performance features: mod-wheel rolls, the Kalimba's hole cover, the Bala 7-equal and 5-equal pentatonic tunings and Note-off Damp (tuning, note-off mode and wheel events are recorded per render).

These are native engine throughput measurements, including parameter updates and event scheduling. They exclude encoding, file IO, the Rack wrapper, Reason scheduling and target-chip performance; they are not a host CPU/deadline guarantee.

Machine: `macOS-26.5.1-arm64-arm-64bit / arm64`. Compiler: `Apple clang version 21.0.0 (clang-2100.1.1.101)`.
DSP source digest: `8758f28022b46859e87f314b3b52bf6d72c2354af89d753271f0000da17ad1b8`.

Reproduce with `python3 Tools/Demos/run_benchmark.py`. WAV/AAC files and the full local player are written into ignored `Tools/Demos/Audio/`. The committed [demo player](../audio/demo-player.html) is self-contained and rebuilt by the same command. [Its manifest](../audio/demo-player-manifest.json) records every input and output SHA-256, codec peak and raw renderer metric.

The twelve previously shipped repertoire comparisons are embedded, together with twenty-eight original touch studies and six idiom studies. Original Touch studies set Variance and Artifacts to zero; subtle intrinsic variation remains. Each comparison reuses its fixed input event sequence. Bar position comparisons hold MIDI note, velocity and gain constant at centre (50%), off-centre (35%), the fundamental node (22.42%) and edge (10%). Kalimba comparisons use control positions 10%, 35%, 50% and 90% along the playable end (physical tine range 65–100%). Dynamics studies play MIDI velocities 16, 32, 64, 96 and 127 at fixed gain; four same-note Kalimba studies isolate the thumb/pluck choice. Four repeated-hit studies each play twelve C4 notes at MIDI velocity 90, spaced 500 ms apart, followed by three seconds of decay. Their controls remain fixed and no per-note normalization is applied. The idiom studies are original phrases: a rolled chorale (Rosewood, soft yarn), a melody in thirds with rolled long notes (Padauk), a bala ostinato in the Bala 7-equal tuning and interlocking parts in the 5-equal pentatonic tuning (Balafon), a thumb pattern with the Kalimba's sound holes opened and closed three times a second, and a phrase played legato and staccato with Note-off Damp (Rosewood). These comparisons reveal timbre and response; they do not establish acoustic equivalence to a recorded instrument.

Classical MIDI adaptations retain their source credits and share-alike licences; see [third-party notices](../../THIRD_PARTY_NOTICES.md). Existing soundfont reference recordings remain unmodified and are not incorporated into the synthesis or these renders.

| Track | Model | Striker / position | Duration | Native render | Peak / RMS | Decoded AAC peak |
|---|---|---|---:|---:|---:|---:|
| J.S. Bach - Prelude in C Major, BWV 846 | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 44.9s | 27.7× | -10.8 / -24.1 dBFS | -10.8 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 44.9s | 28.8× | -11.5 / -26.7 dBFS | -11.4 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Balafon Ancestral | Hard Rubber / 50% | 44.9s | 26.4× | -4.2 / -24.2 dBFS | -4.5 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Kalimba Artisan 17-Key | Natural Thumb / 50% | 44.9s | 20.6× | -11.0 / -24.7 dBFS | -10.9 dBFS |
| L. v. Beethoven - Für Elise (Poco moto) | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 44.9s | 20.7× | -14.4 / -30.0 dBFS | -14.4 dBFS |
| L. v. Beethoven - Für Elise (Poco moto) | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 44.9s | 24.3× | -14.7 / -31.3 dBFS | -14.8 dBFS |
| L. v. Beethoven - Für Elise (Poco moto) | Balafon Ancestral | Hard Rubber / 50% | 44.9s | 22.2× | -5.4 / -27.8 dBFS | -5.7 dBFS |
| L. v. Beethoven - Für Elise (Poco moto) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 44.9s | 15.3× | -13.5 / -29.2 dBFS | -13.3 dBFS |
| Scott Joplin - The Entertainer (Ragtime Two-Step) | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 44.8s | 8.2× | -2.4 / -19.4 dBFS | -2.4 dBFS |
| Scott Joplin - The Entertainer (Ragtime Two-Step) | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 44.8s | 12.5× | -2.6 / -21.9 dBFS | -2.4 dBFS |
| Scott Joplin - The Entertainer (Ragtime Two-Step) | Balafon Ancestral | Hard Rubber / 50% | 44.8s | 11.6× | -2.5 / -17.9 dBFS | -1.5 dBFS |
| Scott Joplin - The Entertainer (Ragtime Two-Step) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 44.8s | 8.6× | -2.8 / -22.8 dBFS | -2.8 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 45.0s | 20.8× | -12.9 / -31.1 dBFS | -13.0 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 45.0s | 25.0× | -9.2 / -34.7 dBFS | -9.3 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Balafon Ancestral | Hard Rubber / 50% | 45.0s | 24.1× | -3.4 / -32.3 dBFS | -3.5 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 45.0s | 18.0× | -12.3 / -32.4 dBFS | -12.3 dBFS |
| W.A. Mozart - Piano Sonata No. 16 in C Major, K. 545 | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 45.0s | 20.4× | -5.6 / -23.2 dBFS | -5.6 dBFS |
| W.A. Mozart - Piano Sonata No. 16 in C Major, K. 545 | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 45.0s | 22.6× | -4.3 / -24.2 dBFS | -4.3 dBFS |
| W.A. Mozart - Piano Sonata No. 16 in C Major, K. 545 | Balafon Ancestral | Hard Rubber / 50% | 45.0s | 20.2× | -2.9 / -20.2 dBFS | -1.9 dBFS |
| W.A. Mozart - Piano Sonata No. 16 in C Major, K. 545 | Kalimba Artisan 17-Key | Natural Thumb / 50% | 45.0s | 13.6× | -7.0 / -23.7 dBFS | -7.0 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 44.9s | 28.2× | -10.8 / -24.1 dBFS | -10.8 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Imperial Rosewood 5.0 | Medium Concert Cord / 50% | 44.9s | 28.2× | -10.1 / -22.1 dBFS | -10.1 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Imperial Rosewood 5.0 | Hard Rubber / 50% | 44.9s | 28.1× | -4.1 / -20.5 dBFS | -4.2 dBFS |
| J.S. Bach - Prelude in C Major, BWV 846 | Imperial Rosewood 5.0 | Wood Baton / 50% | 44.9s | 26.5× | -1.6 / -17.9 dBFS | -1.6 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Kalimba Artisan 17-Key | Thumb Flesh Pad / 50% | 45.0s | 18.5× | -12.8 / -35.0 dBFS | -12.9 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 45.0s | 18.4× | -12.3 / -32.4 dBFS | -12.3 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Kalimba Artisan 17-Key | Thumbnail Snap / 50% | 45.0s | 18.4× | -12.3 / -32.3 dBFS | -12.3 dBFS |
| L. v. Beethoven - Moonlight Sonata (Adagio sostenuto) | Kalimba Artisan 17-Key | Thumb Pick / 50% | 45.0s | 18.1× | -12.4 / -32.3 dBFS | -12.5 dBFS |
| C4 · centre (50%) | Imperial Rosewood 5.0 | Medium Concert Cord / 50% | 5.2s | 58.6× | -7.0 / -23.0 dBFS | -7.1 dBFS |
| C4 · off-centre (35%) | Imperial Rosewood 5.0 | Medium Concert Cord / 35% | 5.2s | 58.2× | -9.2 / -25.6 dBFS | -9.1 dBFS |
| C4 · fundamental node (22.42%) | Imperial Rosewood 5.0 | Medium Concert Cord / 22.42% | 5.2s | 56.9× | -9.3 / -37.7 dBFS | -9.3 dBFS |
| C4 · edge (10%) | Imperial Rosewood 5.0 | Medium Concert Cord / 10% | 5.2s | 57.2× | -4.2 / -28.1 dBFS | -4.3 dBFS |
| C4 · pp to ff (16 → 127) | Imperial Rosewood 5.0 | Medium Concert Cord / 50% | 7.4s | 57.3× | -3.3 / -23.4 dBFS | -3.3 dBFS |
| C4 · centre (50%) | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 5.2s | 57.9× | -9.4 / -29.2 dBFS | -9.5 dBFS |
| C4 · off-centre (35%) | Mayan Padauk 4.3 | Medium Concert Cord / 35% | 5.2s | 58.5× | -10.9 / -30.2 dBFS | -9.3 dBFS |
| C4 · fundamental node (22.42%) | Mayan Padauk 4.3 | Medium Concert Cord / 22.42% | 5.2s | 58.8× | -5.6 / -36.0 dBFS | -5.5 dBFS |
| C4 · edge (10%) | Mayan Padauk 4.3 | Medium Concert Cord / 10% | 5.2s | 57.9× | -4.2 / -33.5 dBFS | -4.3 dBFS |
| C4 · pp to ff (16 → 127) | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 7.4s | 56.5× | -3.8 / -29.3 dBFS | -3.8 dBFS |
| C4 · centre (50%) | Balafon Ancestral | Medium Concert Cord / 50% | 5.2s | 56.2× | -5.8 / -30.2 dBFS | -6.0 dBFS |
| C4 · off-centre (35%) | Balafon Ancestral | Medium Concert Cord / 35% | 5.2s | 56.7× | -8.1 / -30.9 dBFS | -8.1 dBFS |
| C4 · fundamental node (22.42%) | Balafon Ancestral | Medium Concert Cord / 22.42% | 5.2s | 57.7× | -8.7 / -34.8 dBFS | -8.7 dBFS |
| C4 · edge (10%) | Balafon Ancestral | Medium Concert Cord / 10% | 5.2s | 57.1× | -3.2 / -29.3 dBFS | -3.1 dBFS |
| C4 · pp to ff (16 → 127) | Balafon Ancestral | Medium Concert Cord / 50% | 7.4s | 55.2× | -3.2 / -30.1 dBFS | -3.3 dBFS |
| C4 · middle (50%) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 5.2s | 71.5× | -16.1 / -28.6 dBFS | -16.1 dBFS |
| C4 · lower tine (35%) | Kalimba Artisan 17-Key | Natural Thumb / 35% | 5.2s | 73.3× | -16.4 / -29.0 dBFS | -16.4 dBFS |
| C4 · inner contact (10%) | Kalimba Artisan 17-Key | Natural Thumb / 10% | 5.2s | 72.4× | -16.9 / -29.6 dBFS | -16.9 dBFS |
| C4 · near tip (90%) | Kalimba Artisan 17-Key | Natural Thumb / 90% | 5.2s | 73.4× | -15.1 / -27.7 dBFS | -15.2 dBFS |
| C4 · pp to ff (16 → 127) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 7.4s | 72.2× | -13.0 / -28.5 dBFS | -13.0 dBFS |
| C4 · Thumb Flesh Pad | Kalimba Artisan 17-Key | Thumb Flesh Pad / 50% | 5.2s | 73.4× | -15.9 / -29.7 dBFS | -15.9 dBFS |
| C4 · Natural Thumb | Kalimba Artisan 17-Key | Natural Thumb / 50% | 5.2s | 73.4× | -16.1 / -28.6 dBFS | -16.1 dBFS |
| C4 · Thumbnail Snap | Kalimba Artisan 17-Key | Thumbnail Snap / 50% | 5.2s | 73.8× | -16.2 / -28.5 dBFS | -15.6 dBFS |
| C4 · Thumb Pick | Kalimba Artisan 17-Key | Thumb Pick / 50% | 5.2s | 73.6× | -16.3 / -28.5 dBFS | -16.4 dBFS |
| Repeated C4 · fixed velocity (90) | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 8.5s | 57.5× | -7.7 / -19.4 dBFS | -7.0 dBFS |
| Repeated C4 · fixed velocity (90) | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 8.5s | 56.6× | -9.1 / -25.3 dBFS | -9.2 dBFS |
| Repeated C4 · fixed velocity (90) | Balafon Ancestral | Hard Rubber / 50% | 8.5s | 54.8× | -3.2 / -23.8 dBFS | -2.7 dBFS |
| Repeated C4 · fixed velocity (90) | Kalimba Artisan 17-Key | Natural Thumb / 50% | 8.5s | 69.7× | -14.6 / -25.7 dBFS | -14.6 dBFS |
| Rolled chorale · mod-wheel roll | Imperial Rosewood 5.0 | Soft Wool Yarn / 50% | 14.0s | 19.5× | -2.6 / -15.7 dBFS | -2.5 dBFS |
| Melody in thirds · redoble rolls | Mayan Padauk 4.3 | Medium Concert Cord / 50% | 9.0s | 29.0× | -3.2 / -18.0 dBFS | -3.1 dBFS |
| Bala ostinato · Bala 7-equal tuning | Balafon Ancestral | Hard Rubber / 50% | 12.9s | 23.0× | -2.9 / -21.0 dBFS | -2.8 dBFS |
| Interlocking parts · 5-equal pentatonic | Balafon Ancestral | Wood Baton / 50% | 13.9s | 15.2× | -1.5 / -16.5 dBFS | -1.2 dBFS |
| Thumb pattern · hole-cover wah | Kalimba Artisan 17-Key | Natural Thumb / 50% | 15.0s | 23.8× | -7.6 / -21.7 dBFS | -7.7 dBFS |
| Legato and staccato · Note-off Damp | Imperial Rosewood 5.0 | Medium Concert Cord / 50% | 9.4s | 46.9× | -3.3 / -22.1 dBFS | -3.3 dBFS |
