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
                // Mode initial amplitude weights (calibrated against acoustic references)
                { 1.000f, 0.280f,  0.070f,  0.024f,  0.010f,  0.005f,  0.002f,  0.001f },
                // Physics-based damping: alpha_rad, alpha_int
                0.18f,    // radiation: modes radiate ∝ f^2 (fast upper mode radiation)
                0.75f,    // internal: viscoelastic loss ∝ f
                13.0f,    // realistic acoustic brass resonator tube Q
                0.60f,    // balanced acoustic coupling without hollow whistling
                false,    // no mirliton
                0.0f,
                0.0f,
                3.0f,     // singing rosewood fundamental sustain
                0.65f,
                4200.0f,  // clack center frequency
                2800.0f,  // clack bandwidth
                0.0008f,
                // Premium phenomena:
                0.05f,    // subtle natural acoustic tension glide
                1.0f,     // subtle rosewood anisotropic split (1.0 cent beating)
                1.30f,    // pronounced mallet felt contact damping
                86.0f,    // 86 Hz mahogany frame resonance
                1.2f,     // well-damped frame Q
                15.0f     // hand-damp rate (~65 ms decay)
            };
            
        case EMarimbaModel::MayanPadauk:
            return {
                "Mayan Padauk 4.3",
                "Quarter-sawn Mexican Padauk (Hormiguillo)",
                "Square handcrafted cedar soundboxes with beeswax plugs",
                { 1.000f, 4.000f,  9.800f, 15.600f, 21.400f, 28.200f, 36.000f, 44.800f },
                { 1.000f, 0.340f,  0.090f,  0.030f,  0.012f,  0.006f,  0.003f,  0.001f },
                0.22f,    // slightly higher radiation damping (denser wood)
                0.90f,    // higher internal friction = tight punchy decay
                10.5f,    // warm wooden cedar soundbox Q
                0.52f,
                false,
                0.0f,
                0.0f,
                2.0f,     // punchy, warm sustain
                0.75f,
                3600.0f,
                2200.0f,
                0.0016f,
                // Premium phenomena:
                0.04f,
                1.2f,     // padauk anisotropic split
                1.15f,
                110.0f,   // 110 Hz cedar frame body resonance
                1.2f,
                14.0f
            };
            
        case EMarimbaModel::BalafonAncestral:
            return {
                "Balafon Ancestral",
                "Fire-cured African Ironwood (Kene)",
                "Natural calabash gourds with vibrating mirliton membranes",
                { 1.000f, 4.000f,  9.600f, 14.100f, 19.500f, 25.800f, 33.200f, 41.600f },
                { 1.000f, 0.380f,  0.110f,  0.040f,  0.016f,  0.008f,  0.004f,  0.001f },
                0.25f,    // higher radiation from irregular bar shape
                1.10f,    // high internal friction from fire-cured wood
                9.0f,     // organic gourd cavity
                0.48f,
                true,     // active vibrating mirliton membrane
                0.085f,   // membrane displacement threshold (buzz on forte/accents)
                2.0f,     // buzz sizzle drive
                1.6f,     // crisp, percussive sustain
                0.85f,
                4800.0f,
                3200.0f,
                0.0028f,
                // Premium phenomena:
                0.06f,
                0.4f,
                0.90f,
                145.0f,   // 145 Hz bamboo frame & rail resonance
                1.2f,
                12.0f
            };

        case EMarimbaModel::KalimbaArtisan:
        default:
            return {
                "Kalimba Artisan 17-Key",
                "Solid African Acacia wood soundbox with pyrography rosette",
                "Ergonomic spring steel tines on brass and hardwood bridge",
                // Clamped cantilever beam overtone ratios (Euler-Bernoulli theory)
                { 1.000f, 6.267f, 17.550f, 34.390f, 56.840f, 83.900f, 116.700f, 155.300f },
                { 1.000f, 0.220f,  0.045f,  0.015f,  0.006f,  0.002f,  0.001f,  0.0005f },
                0.025f,   // low radiation damping (narrow tine, high modes sing out)
                0.12f,    // low internal damping (spring steel)
                10.0f,    // acoustic soundbox Q
                0.45f,    // body cavity coupling
                false,    // no buzz membrane
                0.0f,
                0.0f,
                4.0f,     // crystalline tine sustain
                0.55f,
                5400.0f,  // bright steel tine release ping
                3500.0f,  // clack bandwidth
                0.0004f,
                // Premium phenomena:
                0.03f,
                0.0f,     // steel tines have no anisotropic grain split
                0.80f,
                245.0f,   // 245 Hz acacia cavity air bloom
                1.6f,
                22.0f
            };
    }
}

} // namespace maremba
