# Coupled physical models and clear shared resonance

This records the first pass. See the [second pass](physical-models-round2.md)
for the current directional radiation, continuous multimode coupling and CPU
measurements; the first-pass source snapshots and validation remain preserved.

This is an intentional sound update to the current 1.3 development tree. Stored
property identities, device defaults, ranges and the default 2x quality stay
unchanged. Two factory patch volumes are recalibrated: Ambient Cathedral Bloom
0.91 → 0.97, Midnight Village Ritual 0.82 → 0.90, restoring the bank's established
loudness window after the physical update. Other preset values stay unchanged.
Existing songs can sound different. No release/version or portal upload is
implied by this DSP change.

## Audible changes

- **Imperial Rosewood:** the fundamental exchanges energy with a damped air
  column instead of driving an independent output filter. Detuning changes the
  pressure returned to the bar; tube modes follow a closed/open 1:3:5 sequence.
- **Mayan Padauk:** broader, irregular box modes and shorter wooden support
  response retain the existing stronger upper bar partials and woody attack.
- **Balafon:** a dominant gourd resonance and lossy upper air modes feed a dynamic
  nonlinear membrane with inertia, damping and amplitude-dependent tension.
- **Kalimba:** broad box modes near 245, 566 and 1022 Hz stay fixed across the
  keyboard. Their response and feedback now depend on how close each tine is to
  a body resonance. Flesh/nail/pick contact still acts as progressive release
  from static deflection, distinct from a mallet impact.

The pressure/velocity coupling uses an orthogonal rotation of normalized modal
states. Oscillator losses remove energy; the junction itself preserves the
squared state norm. This makes feedback stable without clipping the ringing
bar to conceal an unstable solver. The membrane uses a discrete-gradient
nonlinear oscillator update, with positive hardening potential and damping.
These are efficient reduced physical models, not finite-element replicas of
measured instruments. See [primary references and limits](physical-model-references.md).

## Focus and control

The shared support bank now transmits motion according to distance between
keys. Its resonators determine spectral overlap, including adjacent semitones;
the previous perfect-fifth/third whitelist is removed. Wood and steel have
different effective losses. Three model-specific body modes use a
force-to-velocity-shaped band-pass response to normalized mechanical drive,
rejecting DC and attenuating off-resonant bass accumulation. These layers are
still one-way approximations; they do not implement a fully coupled keyboard.

Body, Sympathetic, microphone levels, Width and Drive now approach live changes
with a 5 ms time constant. Their bypass states clear after the audible fade, so
re-enabling a layer cannot restore a frozen tail. Idle/reset changes apply
immediately. Hardness, Position, Decay, Coupling, striker and velocity depths
remain captured per strike. The hidden legacy Glide remains intentionally
inactive. Resonator Tune still works through its stored automation property,
with its strongest effect near the cavity's resonances.

Body level is recalibrated to keep its clearer velocity-shaped readout behind
the played note. The physical control audit checks added-layer audibility,
decay trends, pickup separation, sub-bass energy and automation transitions.
The existing contracts retain velocity, pitch, register, note allocation,
restrike, sample-rate, oversampling, determinism and realtime-allocation gates.

## Validation and listening

Run the complete suite with `bash Tests/run_all_tests.sh`. The new response
contracts supplement the existing tests rather than replacing them. Two obsolete
assumptions were updated: adjacent-key motion is now required in the mesh, with
stronger response at resonance and attenuation away from it;
the Kalimba coupling probe moves from extended bass C3 to C4 near the fixed
box resonance. Making that off-resonant C3 probe equally strong shortened the
real tine sustain, so the test follows the corrected instrument geometry instead
of forcing the old transposing-tube behavior. Its audibility threshold is unchanged.

The before/after listening review uses the same score and common playback gain
for all four instruments, in isolated Close and default Main mixes. Baseline and
updated source snapshots plus SHA-256 manifests identify the rendered code.
Objective tests establish response, stability and bounds; listening is still
needed to judge whether a particular patch is pleasant for its musical context.

The final SDK 5 `local45 Deployment` build passed for x86_64 and arm64, including
the complete mandatory pre-build suite and all 11 patches at 44.1/48/96 kHz.
Both new physical contracts also passed AddressSanitizer and
UndefinedBehaviorSanitizer. The installed development payload and saved copy
match the working tree's resources and built libraries. See the
[validation manifest](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/physical-models.json) and
[before/after audio review](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Release/validation/physical-models-20260922/physics-review.html).
Interactive Reason/Recon playback has not been tested. The in-app browser
crashed during audio playback; no completed subjective listening assessment is
claimed.

The native CPU probe uses 16 voices, 48 kHz, 64-frame blocks and 4096 measured
blocks per model/quality after warmup. On this Apple M1 Max, default 2x median
thread time was 165–200 microseconds per block (12–15% of the 1333-microsecond
deadline), versus 85–98 microseconds before. All four default-quality runs had
zero wall-clock or thread-CPU deadline misses. Optional 8x quality had overruns,
including 55 thread-CPU misses in the worst 4096-block run. These are native
engine measurements, not a Reason/Recon CPU guarantee. Raw baseline, intermediate
and final runs are preserved; the steady rendering path was unchanged by the
subsequent idle-control reset fix.

The exact-zero membrane optimization skips its nonlinear solve only when both
motion and applied force are zero. All eight updated listening takes remained
byte-identical to their unoptimized float renders. Component energy tests cover
passive cavity junctions and dissipative unforced membrane motion; they do not
claim energy conservation of the entire instrument and its one-way readouts.
