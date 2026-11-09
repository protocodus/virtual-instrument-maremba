# Halo radiation per octave, 1.6.3

Up to 1.6.2 each of the 61 sympathetic bars (C2-C7) radiated through its own
capture source, and every live source runs its own binaural Close
convolution and Room taps. With Sympathetic above 0, as on every factory
patch, one note kept about 62 sources running. In 1.6.3 the halo radiates each
octave from its F (`HaloSourceNote`), C7 and Kalimba's tines from their own
keys: six halo sources instead of 61. The owner chose this by ear
([decision](../decisions.md#163--the-halo-radiates-per-octave)).

## Timing

The Rack adapter over its host mock, Reason's 64-frame batches at 44.1 kHz,
30 s per script; each batch is timed with the thread CPU clock and keeps its
minimum over three fresh devices. "Reason-like" is Clang 18 `-O2 -fno-builtin
-fno-vectorize -fno-slp-vectorize -ffp-contract=off`, which approximates the
Jukebox toolchain's scalar code. Intel Xeon @ 2.30 GHz cloud container,
2026-11-09. A 64-frame batch lasts 1451 µs.

| Patch, script | Strike batch, 1.6.2 | Strike batch, 1.6.3 | Held, 1.6.2 | Held, 1.6.3 |
| --- | ---: | ---: | ---: | ---: |
| Concert Grand Rosewood, one key | 847 µs | 300 µs | 383 µs | 107 µs |
| Mayan Dance Padauk, one key | 808 µs | 330 µs | 383 µs | 107 µs |
| Ancestral Balafon, one key | 750 µs | 291 µs | 381 µs | 107 µs |
| Kalimba Thumb Lullaby, one key | 298 µs | 305 µs | 134 µs | 134 µs |
| Concert Grand Rosewood, 8-note chord | 1288 µs | 869 µs | 428 µs | 178 µs |
| Ancestral Balafon, 8-note chord | 1489 µs | 947 µs | 426 µs | 177 µs |

"Held" is the median batch while the key is down. These are
development-machine figures, not Reason or DAW CPU.

## Falling idle

With the halo this cheap, a ringing batch costs about a third of what it did,
and `Maremba.IdleFlushCost` (idle batch at most twice a ringing one) failed
again: natively, 205 µs against a 76 µs ring. The rest of the flush was
`MainPickupAlignment::Reset` clearing every source's alignment line (about
1 MB). A source's line is cleared when it next activates, so Reset now only
retires the sources; the idle batch measures 116 µs against a 76 µs ring.

## Sound

- The shipping 1.6.3 source renders the listening set's C files byte for byte
  through the Rack adapter.
- The pickup change is bit-identical: `Tools/CpuEquivalenceRender.cpp`
  (32 cases) gives the same SHA-256 with and without it
  (`b8d5dd75bcc7fed65d6aa71fe49ca140c1f5f919d1727731b908b8f61819d34c`, GCC 13,
  `-std=c++17 -O3`, x86-64), and so does a hash of every output, direct outs
  included, over two idle flushes and a restrike on each model.
