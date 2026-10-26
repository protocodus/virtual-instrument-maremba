# Physical parameter audit

Audited on 2026-09-22 against `motherboard_def.lua`, `Maremba.cpp`,
`DSP/MarembaEngine.*`, the voice/model code and existing audio/host contracts.
SDK authority: `JukeboxSDK_500_028`, `TargetVersion=5.0`.

The physical constants are voiced approximations, not fitted measurements of
particular instruments. Audio contracts establish audible behavior, stability and
direction; they cannot establish maximum realism or replace listening comparisons.

| Input | Intended audible behavior and range | Evidence |
| --- | --- | --- |
| Model | Rosewood sustains; Padauk has stronger woody upper modes and shorter sustain; Balafon has gourd/membrane color; Kalimba uses plucked cantilever tines. | `TestWoodModelSeparation`, `TestKalimbaSustain`, touch/physical response tests. |
| Striker, Hardness | Four material/release profiles; harder contact shortens force duration and changes the attack spectrum. Kalimba changes thumb-release behavior. | `TestStrikerHierarchy`, `TouchResponseTest`, `EngineRealismTest`. |
| Position | Signed beam shapes suppress modes at nodes; tine position traverses its playable tip region. A brightness reversal near a node is physically meaningful. | `TestPhysicalStrikePositions`, `TouchResponseTest` node/parity and restrike checks. |
| Velocity curve, note/gate velocity | Soft increases light-touch level, Hard requires more force, Expressive expands midrange. Force also changes contact spectrum. | `TestVelocityStrictlyMonotonic`, `TestNoDeadParameters`, `TouchResponseTest`, wrapper event contracts. |
| Velocity → Hardness/Position/Decay | 0–100% depth; after velocity mapping, scale the base value by `1 − depth × (1 − velocity)`. Full velocity preserves the knob value. | `TestVelocityDepths` verifies destinations, independence, note-on/restrike timing and unchanged base knobs. |
| Decay | 0.1–8 times model/register sustain; longer values increase late/early energy, not merely volume. | New `PhysicalResponseTest` at MIDI 48/72 and 0.4/1/2.5; `TestMaxDecayRateInvariant`. |
| Coupling | Zero disconnects the air cavity; larger settings exchange more mechanical/air energy and alter damping/radiation. | `EngineRealismTest` response; new physical tune/disconnect contracts and isolated-pickup reaction to cavity tuning. |
| Resonator Tune | Legacy ±50-cent property remains active on new strikes. Audible when coupled; no effect at zero coupling. | New `PhysicalResponseTest` compares ±50 after best gain fit and checks exact zero-coupling independence. |
| Buzz | Amplitude-dependent acoustic membrane/attachment texture, strongest on Balafon; zero disables it; follows ringing notes. | `TestMirlitonBuzz`, `TestCharacterRangeAndLiveBuzz`; new isolated layer/amount/tail checks. |
| Artifacts | Probability/severity of imperfect contacts, rebounds, adjacent-key brushes and mount/bridge collisions; zero disables events. | `MechanicalArtifactsTest`, `CollisionResponseTest`, 64-strike ensemble in `EngineRealismTest`. |
| Variance | 0–100 expands deterministic per-strike contact/partial variation. Zero retains restrained organic variation, rather than repeating identical waveforms. | `TestStrikeJitter`, `TestOrganicRepeatVariation`, reset contracts. |
| Body Bloom, Sympathetic | Shared acoustic frame and unstruck-key response; adds audible decaying energy to Close/Far without entering the isolated contact pickup; 5 ms live smoothing. | New isolated wet-minus-dry, held-input DC rejection and automation contracts; existing bypass, rate, reset and shared-tail checks. |
| Close, Room, Piezo | Independent smoothed mix gains; ROOM also controls the room return. Direct Close/Piezo remain pre-fader; direct Far includes the room return. | `TestVelocityAndMixerSilence`, `TestDirectOutLevels`, `TestDirectOutMatchesMain`, wrapper routing; new automation contracts. |
| Space | Wood, Chamber, Hall: different stereo reflections/tails, live crossfade. Inert when Room is zero. | `TestRoomAlgorithms`, `TestRoomBypassNoStaleTail`, wrapper room mapping. |
| Width | 0 mono, 1 natural, 2 widened dry image; room side gain stops at 1; 5 ms smoothing. Direct taps unaffected. | `TestStereoWidth`, `TestWidthAndRoomImage`, new automation contract. |
| Drive, Warmth | Smoothed saturation amount and −1…+1 tilt; main bus only. Warmth shelf endpoints span about ±6 dB. | `TestPreampLimiterContainment`, `TestWarmthRange`, parameter reachability and new Drive automation contract. |
| Limit, Attack, Release | Limiter threshold/amount, 1–50 ms attack and 20–500 ms initial recovery; Attack/Release require active limiting to matter. | `TestAnalogLimiterCurveAndLink`, `TestAnalogLimiterBallistics`, `TestAnalogLimiterState`. |
| Volume | Squared amplitude law, 5 ms smoothing, Main L/R only (the direct outputs ignore it from 1.4.0f5). | `TestVolumeSmoothing`, `TestVolumeChangeWhileIdle`, `DirectOutputLevelTest`. |
| Tune, Reason master tune, pitch bend | Device ±100 cents plus host ±100; bend ±200 cents. Ringing bars and sympathetic resonators retune without pumping. | `TestPitchAccuracy`, `TestPitchBendRetunesRingingNotes`, `TestBendModulationStability`, wrapper tune/bend. |
| Detune | 0–100 cents maximum fixed per-key offset; a repeated key retains its tuning. | `TestDetune`, reset and tuning contracts. |
| Quality | 2x/4x/8x: numerical/aliasing policy, rather than an intended tone control; preserve level, tuning and decay. Live changes fade/reset safely. | `TestRateInvariance`, `TestOversamplingSwitch`, max-decay/rate tests. |
| Voices | 8/16/24 simultaneous notes; audible only when voice allocation is constrained; stolen voices fade. | Polyphony, steal-victim, queued-chord and burst tests. |
| CV and mod wheel | Note/Gate events; additive clamped Hardness/Position/Coupling/Sympathetic/Volume CV; mod wheel adds up to 0.35 hardness. | `WrapperHostTest` mapping and sample-accurate event contracts. |
| Note-off, transport stop | Note-off preserves the natural ring; transport stop applies model-specific gentle hand damping. | `TestNoteOffIsSoundNeutral`, `TestDampAllIsGentle`, queued damping tests. |
| Legacy Glide | Intentionally ignored for compatibility with the already removed pitch sweep; not an active control. | `TestLegacyPitchGlideIgnored`. |

Strike properties are captured at note-on/restrike: model, striker, hardness,
position, cavity settings, decay, artifacts, variance, detune and velocity depths.
Their automation is expected to affect subsequent strokes. Buzz, shared acoustic
levels, mixer, tone/dynamics, volume and global tuning operate on ringing sound.
Silent combinations are intentional: a disconnected cavity cannot respond to its
tune control, a bypassed limiter cannot respond to its timing, and a closed room
send cannot reveal the selected room.

`PhysicalResponseTest` uses a fixed seed, 48 kHz/2x, direct mono Close and the
independent Piezo tap, with room/preamp/limiter/variance/artifacts/velocity routing
disabled. Decay uses 300–600 ms energy divided by 20–80 ms energy. Tune uses the
residual after the best constant-gain fit at MIDI 60, which is near the fixed
Kalimba box resonance; the tuning effect is not expected to be equally strong
on every key. The isolated Piezo must also change, demonstrating the cavity's
reaction on bar motion. Character layers use matched
wet-minus-dry audio: endpoint audibility, increasing amount, sub-25 Hz fraction,
700–1000 ms versus 40–300 ms decay, and unchanged Piezo are checked separately.
The broad gates are regression limits, not perceptual rankings or laboratory
measurements. Held-input probes verify that a static input to Body/Sympathetic
does not keep radiating. Seven live-control probes compare the first 2 ms of an
automated move against its full instantaneous step, then verify target settling.
`--measure` records observations without enforcing the normal acceptance gates.

Reproduce the focused contract from the project directory:

```sh
clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I DSP \
  Tests/PhysicalResponseTest.cpp DSP/*.cpp -o /tmp/maremba-physical-response
/tmp/maremba-physical-response
```

The integrated contract passed. A comparison against the saved pre-change DSP
snapshot used the same test source, changing only the two shared-block
`Configure` calls to their former signatures. It demonstrates that the new
contracts distinguish the physical/automation changes from the previous model:

| Measured response | Before | Revised |
| --- | ---: | ---: |
| Isolated Piezo residual when only cavity tuning changes, four models | Exactly 0 | 0.0018–0.0350 |
| Sympathetic output from held DC, final/initial 200 ms | −0.03 dB | Below −97 dB on every model |
| First 2 ms automation energy versus immediate full step, seven live controls | −1.9 to +0.5 dB | Below −13 dB on every control |
| Automation target error after 50 ms | Below −77 dB | Below −81 dB |

At full amount, isolated Body, Sympathetic and Buzz contributions stay between
approximately −31 and −12 dB relative to the dry bar for the controlled C4
strike. Their sub-25 Hz fraction stays below −37 dB, and their last 300 ms is
at least 16 dB below the early ring. All eight model/register decay sweeps
increase normalized sustain at each step. These measurements apply to the
specific deterministic probe above, not every musical phrase or a listening
score. The complete test prints each model/control result for review.

Two additional regression probes mute the microphones just before a quality
switch or natural idle boundary, then wait one second and play a new note
without refreshing the controls. Both require exact silence. They detected
stale smoothed gains that otherwise froze while idle; the reset now snaps live
controls at both boundaries. The pre-fix peaks were 0.0525 and 0.1372.

Independent source review also checked upper-air-mode lifecycle/reset/retuning,
state preservation on live model changes, release of disabled membranes, fixed
Kalimba cavity dimensions under pitch bend, delayed clearing after shared-control
bypass fades, and the absence of new render-time allocation. Main/residual
restrike states exchange energy with the cavity through separate passive
rotations; their different contact damping therefore cannot invalidate the
individual junction invariants. Exact junction passivity and physical-component
ablation are covered separately by `CavityPhysicsTest`.
