#pragma once

#include <cmath>
#include <cstdint>

namespace maremba {

enum class EMarimbaModel : uint8_t {
    ImperialRosewood = 0,
    MayanPadauk = 1,
    BalafonAncestral = 2,
    KalimbaArtisan = 3,
    Count = 4
};

// Number of pitched modal resonators per voice
static constexpr int kNumPitchedModes = 8;
// Total modes = pitched + 1 clack noise burst
static constexpr int kNumTotalModes = kNumPitchedModes + 1;

struct ModelParams {
    const char* name;
    const char* woodDescription;
    const char* resonatorDescription;
    
    // Overtone ratios relative to fundamental f0 (8 pitched modes)
    float overtoneRatio[kNumPitchedModes];
    // Mode initial amplitude weights
    float modeWeights[kNumPitchedModes];
    
    // Physics-based damping coefficients (replace fixed modeDamping[] array)
    float alphaRadiation;    // Radiation (air) damping coefficient: damping ∝ (f/f0)^2
    float alphaInternal;     // Internal (material) viscoelastic damping: damping ∝ (f/f0)
    
    // Resonator air column properties
    float resonatorQ;
    float couplingStrength;
    
    // Mirliton / buzz properties
    bool hasMirliton;
    float mirlitonThreshold;
    float mirlitonDrive;
    
    // Material acoustic properties
    float baseDecaySec;      // Bass bar sustain duration in seconds
    float trebleDampFactor;  // How much faster high notes decay compared to bass
    float clackFrequency;    // Center frequency of strike clack noise burst
    float clackBandwidth;    // Bandwidth of clack noise burst (Hz)
    float woodInternalFriction;

    // Premium physical modeling phenomena
    float tensionModFactor;      // Attack pitch glide / deflection non-linearity depth
    float anisotropicSplitCents; // Dual-mode orthogonal bending split for singing beating
    float contactDampingFactor;  // Viscoelastic mallet damping of overtones during contact
    float frameBodyFreq;         // Low-frequency frame & rail resonance frequency (Hz)
    float frameBodyQ;            // Frame body acoustic Q
    
    // Note-off hand-damping speed (higher = faster damping)
    float handDampRate;          // Exponential rate constant for hand-damping on note-off
};

inline ModelParams GetModelParams(EMarimbaModel model) {
    switch (model) {
        case EMarimbaModel::ImperialRosewood:
            return {
                "Imperial Rosewood 5.0",
                "Aged Honduran Rosewood (Dalbergia stevensonii)",
                "Gold-finished drawn brass oval resonator tubes",
                // 8 pitched overtone ratios: modes 0-7
                // Measured from Bork (1995) & Chaigne (1999) for undercut rosewood bars
                { 1.000f, 4.000f, 10.000f, 16.000f, 22.400f, 29.600f, 37.800f, 47.200f },
                // Mode initial amplitude weights (diminish with mode number)
                { 1.000f, 0.450f,  0.280f,  0.140f,  0.075f,  0.040f,  0.022f,  0.012f },
                // Physics-based damping: alpha_rad, alpha_int
                0.0018f,  // radiation: modes radiate ∝ f^2
                0.035f,   // internal: viscoelastic loss ∝ f
                28.0f, // high Q brass resonators
                0.85f, // strong acoustic coupling
                false, // no mirliton
                0.0f,
                0.0f,
                3.2f,  // long, singing rosewood sustain
                0.65f,
                4200.0f,   // clack center frequency
                2800.0f,   // clack bandwidth
                0.0008f,
                // Premium phenomena:
                0.08f,  // subtle natural acoustic tension glide
                1.5f,   // ★ ENABLED: rosewood anisotropic split (1.5 cents beating)
                1.30f,  // pronounced mallet felt contact damping
                86.0f,  // 86 Hz mahogany frame resonance
                1.5f,   // well-damped frame Q
                15.0f   // hand-damp rate (~65 ms decay)
            };
            
        case EMarimbaModel::MayanPadauk:
            return {
                "Mayan Padauk 4.3",
                "Quarter-sawn Mexican Padauk (Hormiguillo)",
                "Square handcrafted cedar soundboxes with beeswax plugs",
                { 1.000f, 4.000f,  9.800f, 15.600f, 21.400f, 28.200f, 36.000f, 44.800f },
                { 1.000f, 0.500f,  0.320f,  0.160f,  0.085f,  0.045f,  0.025f,  0.014f },
                0.0024f,  // slightly higher radiation damping (denser wood)
                0.045f,   // higher internal friction
                18.0f, // warm wooden resonator Q
                0.72f,
                false,
                0.0f,
                0.0f,
                2.2f,  // punchy, warm sustain
                0.75f,
                3600.0f,
                2200.0f,
                0.0016f,
                // Premium phenomena:
                0.06f,  // subtle punchy woody pitch attack
                2.0f,   // ★ ENABLED: padauk anisotropic split (2.0 cents, more beating)
                1.15f,
                110.0f, // 110 Hz cedar frame body resonance
                1.5f,   // well-damped frame Q
                14.0f   // hand-damp rate (~70 ms decay)
            };
            
        case EMarimbaModel::BalafonAncestral:
            return {
                "Balafon Ancestral",
                "Fire-cured African Ironwood (Kene)",
                "Natural calabash gourds with vibrating mirliton membranes",
                { 1.000f, 4.000f,  9.600f, 14.100f, 19.500f, 25.800f, 33.200f, 41.600f },
                { 1.000f, 0.600f,  0.380f,  0.200f,  0.110f,  0.060f,  0.035f,  0.020f },
                0.0030f,  // higher radiation (more irregular bar shape)
                0.055f,   // high internal friction from fire-cured wood
                12.0f, // organic gourd cavity
                0.65f,
                true,  // active vibrating mirliton membrane!
                0.045f, // membrane displacement threshold for buzz
                3.5f,   // buzz sizzle drive
                1.7f,   // crisp, percussive sustain
                0.85f,
                4800.0f,
                3200.0f,
                0.0028f,
                // Premium phenomena:
                0.10f,  // subtle attack pitch thump
                0.5f,   // ★ ENABLED: minimal ironwood anisotropic split
                0.90f,
                145.0f, // 145 Hz bamboo frame & rail resonance
                1.5f,   // well-damped frame Q
                12.0f   // hand-damp rate (~80 ms, heavier bars)
            };

        case EMarimbaModel::KalimbaArtisan:
        default:
            return {
                "Kalimba Artisan 17-Key",
                "Solid African Acacia wood soundbox with pyrography rosette",
                "Ergonomic spring steel tines on brass and hardwood bridge",
                // Clamped cantilever beam overtone ratios (Euler-Bernoulli theory)
                // f_n/f_1 = (lambda_n / lambda_1)^2 where lambda_n are eigenvalues
                { 1.000f, 6.267f, 17.550f, 34.390f, 56.840f, 83.900f, 116.700f, 155.300f },
                { 1.000f, 0.400f,  0.220f,  0.100f,  0.050f,  0.025f,  0.012f,  0.006f },
                0.0008f,  // low radiation damping (narrow tine, poor radiator)
                0.015f,   // low internal damping (spring steel)
                14.0f, // acoustic soundbox Q
                0.75f, // strong body cavity coupling
                false, // no buzz membrane
                0.0f,
                0.0f,
                4.5f,  // long, singing crystalline tine sustain
                0.55f, // gentle treble damping
                5400.0f,   // bright steel tine release ping
                3500.0f,   // clack bandwidth
                0.0004f, // low internal tine friction
                // Premium phenomena:
                0.05f,  // subtle initial deflection pluck glide
                0.0f,   // ★ DISABLED: steel tines have no anisotropic split
                0.80f,  // rapid thumb flesh release
                245.0f, // 245 Hz acacia cavity air bloom
                2.2f,   // resonant soundbox Q
                22.0f   // hand-damp rate: fast finger damp (~40 ms)
            };
    }
}

} // namespace maremba
