# Natural strikes and quieter artifacts — 1.4.0b3

Every note-on draws a new physical contact character, including a restrike of
an already ringing key. Variance at zero retains the small natural spread in
head stiffness, speed, position, lateral offset and loss; it does not select an
identical strike. Artifacts at zero disables optional mistakes and mounting
noises, while retaining that natural contact variation.

Velocity enters the physical excitation. It changes the mallet's incoming
speed, from which the contact solver derives its force and duration. On a
Kalimba it changes the thumb's force and the tine's held modal deflection before
release. Gentler plucks also take longer to slip free; the additional drag is
smoothly bounded at 0.15 ms before Hardness scaling, keeping the slow Flesh
release balanced. This changes the force-time history and attack spectrum even
when every optional velocity depth is zero. The ten velocity-depth controls add further response to the
base physical velocity behavior. They are not required for velocity to affect
the simulation.

## Subtler, continuously varied artifacts

The b3 release reduces glancing-contact losses, friction, rebound strength,
neighbor brushes and mounting collisions. A graze now retains at least 80% of
the stroke impulse, and the combined planned rebound and brush gains stay
below 25%. This bounds the planned secondary gains; it does not establish
conservation of the rendered contact impulse or energy. Event probabilities
still depend on the instrument, playing strength and Artifacts setting.

Each event also receives independent continuous variation in contact duration,
decay, mounting resonance, clearance and pulse shape. Rebound and brush strength
and timing are no longer tied to the same severity draw. A clean restrike keeps
an existing neighbor or mounting tail's tuning rather than changing its shape.
These sounds are generated from changing physical events, not picked from a
small repeating sample bank.

The event detail generator uses a separate stream seeded from an existing
stroke draw. It leaves the main strike stream's draw count unchanged and keeps
Artifacts-zero contact settings neutral. Successive notes advance the stream
through voice reuse and silence. An explicit audio reset deliberately restarts
it, so replaying the same performance remains reproducible.

## Extended-register safety

An 8 mm voiced travel bound prevents impossible low-Kalimba thumb deflection;
the bound acts on excitation force and held modes together, before sound is
rendered. Buzzer sensitivity stops increasing below the frequency represented
by the longest modeled tine (about 57.77 Hz). Hand damping of sub-audio bar air
modes has a 20 Hz loss floor. These prevent the safety guards from having to
kill otherwise valid notes or retain inaudible tails unnecessarily.

The five direct taps now have a fixed −24.4 dB trim, adding 6 dB of headroom for
extreme factory-preset attacks. Main output has its own mix/preamp/limiter path;
direct taps intentionally bypass those stages. Limiter and fully bypassed
stress levels are recorded separately in the release validation.

## Verification

MechanicalArtifactsTest checks 600,000 event plans, bounded subtlety,
non-repeating active contact shapes and a fixed parent draw count. Existing
collision tests verify all four instruments, contact timing across sample
rates, and transport damping. The seeded Kalimba C5 isolated collision-to-note
RMS maximum falls from 0.16582 to 0.11054 (about 3.5 dB); this is a specific
comparison, not a claimed uniform attenuation of every artifact.

NaturalStrikeResponseTest checks repeated physical strikes and same-random-state
velocity comparisons independently. ExtremeRangeTest and EngineFuzzTest cover
the extended keyboard, parameter limits and lifecycle behavior. Exact results,
source hashes and remaining host-validation limits accompany the release.
