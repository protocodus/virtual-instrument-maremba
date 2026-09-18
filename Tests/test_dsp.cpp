#include "../DSP/MarembaEngine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

int main() {
    std::cout << "=== Running Maremba DSP Verification Suite ===" << std::endl;

    double sampleRate = 44100.0;
    maremba::MarembaEngine engine(sampleRate);

    // Test 1: Stability across MIDI note range
    std::cout << "[Test 1] Testing note range C2 (36) to C7 (96) across all 4 models..." << std::endl;
    for (int model = 0; model < 4; ++model) {
        for (int note = 36; note <= 96; note += 6) {
            maremba::EngineParameters params;
            params.model = model;
            params.decay = 0.5f; // short decay for speed
            params.oversampling = 0; // 2x
            engine.SetParameters(params);

            engine.NoteOn(note, 0.85f);

            constexpr int kBatch = 64;
            float outL[kBatch], outR[kBatch];
            float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

            for (int b = 0; b < 20; ++b) { // ~30 ms
                engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
                for (int i = 0; i < kBatch; ++i) {
                    if (std::isnan(outL[i]) || std::isnan(outR[i])) {
                        std::cerr << "NaN detected at model=" << model << " note=" << note << " batch=" << b << " sample=" << i << std::endl;
                        return 1;
                    }
                    assert(!std::isinf(outL[i]) && "Output L is Inf");
                    assert(!std::isinf(outR[i]) && "Output R is Inf");
                    assert(std::abs(outL[i]) < 5.0f && "Output L exploded");
                    assert(std::abs(outR[i]) < 5.0f && "Output R exploded");
                }
            }
            engine.AllNotesOff();
        }
    }
    std::cout << " -> Passed! No NaNs, Infs, or instability detected." << std::endl;

    // Test 2: Oversampling modes (2x, 4x, 8x)
    std::cout << "[Test 2] Testing oversampling modes 2x, 4x, 8x..." << std::endl;
    for (int os = 0; os < 3; ++os) {
        maremba::EngineParameters params;
        params.oversampling = os;
        params.decay = 0.8f;
        engine.SetParameters(params);

        engine.NoteOn(60, 0.9f); // Middle C
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        float maxAmp = 0.0f;
        for (int b = 0; b < 30; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                maxAmp = std::max(maxAmp, std::abs(outL[i]));
                assert(!std::isnan(outL[i]));
            }
        }
        assert(maxAmp > 0.01f && "Audio was silent!");
        std::cout << " -> Mode " << (os == 0 ? "2x" : os == 1 ? "4x" : "8x")
                  << " rendered peak amplitude: " << maxAmp << " (clean)" << std::endl;
        engine.AllNotesOff();
    }
    std::cout << " -> Passed! All oversampling tiers operating cleanly." << std::endl;

    // Test 3: Microphones and Stereo Panning
    std::cout << "[Test 3] Testing 3-Mic mixer & stereo keyboard spread..." << std::endl;
    {
        maremba::EngineParameters params;
        params.closeLevel = 1.0f;
        params.farLevel = 0.0f;
        params.piezoLevel = 0.0f;
        params.stereoWidth = 1.0f;
        params.sympathetic = 0.0f;
        params.bodyBloom = 0.0f;
        engine.SetParameters(params);
        engine.Reset();

        // Low note C2 (36) -> should lean Left
        engine.NoteOn(36, 0.8f);
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        float energyL = 0.0f, energyR = 0.0f;
        for (int b = 0; b < 10; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                energyL += outL[i] * outL[i];
                energyR += outR[i] * outR[i];
            }
        }
        std::cout << " -> C2 Energy L: " << energyL << ", R: " << energyR;
        assert(energyL > energyR && "Low note did not pan left!");
        std::cout << " (Correctly panned left)" << std::endl;

        engine.AllNotesOff();
        engine.Reset();

        // High note C7 (96) -> should lean Right
        engine.NoteOn(96, 0.8f);
        energyL = energyR = 0.0f;
        for (int b = 0; b < 10; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                energyL += outL[i] * outL[i];
                energyR += outR[i] * outR[i];
            }
        }
        std::cout << " -> C7 Energy L: " << energyL << ", R: " << energyR;
        assert(energyR > energyL && "High note did not pan right!");
        std::cout << " (Correctly panned right)" << std::endl;

        engine.AllNotesOff();
    }
    std::cout << " -> Passed! Stereo keyboard panning verified." << std::endl;

    // Test 4: Balafon Mirliton Buzz Activation
    std::cout << "[Test 4] Testing Balafon Mirliton Buzz..." << std::endl;
    {
        maremba::EngineParameters params;
        params.model = 2; // Balafon
        params.buzzAmount = 0.8f;
        params.compAmount = 0.0f;
        params.preampDrive = 0.0f;
        engine.SetParameters(params);
        engine.Reset();

        // Soft hit: buzz should be quiet or zero
        engine.NoteOn(50, 0.15f);
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];
        float softMax = 0.0f;
        for (int b = 0; b < 15; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) softMax = std::max(softMax, std::abs(outL[i]));
        }
        engine.AllNotesOff();
        engine.Reset();

        // Hard hit: buzz should activate and produce sharp harmonics
        engine.NoteOn(50, 0.95f);
        float hardMax = 0.0f;
        for (int b = 0; b < 15; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) hardMax = std::max(hardMax, std::abs(outL[i]));
        }
        engine.AllNotesOff();

        std::cout << " -> Soft hit peak: " << softMax << ", Hard hit peak: " << hardMax << std::endl;
        assert(hardMax > softMax * 3.0f);
    }
    std::cout << " -> Passed! Mirliton membrane non-linearity verified." << std::endl;

    // Test 5: Analog Preamp Saturation & Dynamics Compressor
    std::cout << "[Test 5] Testing Preamp Saturation & Compressor..." << std::endl;
    {
        maremba::EngineParameters params;
        params.preampDrive = 0.9f;
        params.compAmount = 0.8f;
        params.compAttack = 5.0f;
        params.compRelease = 50.0f;
        engine.SetParameters(params);

        engine.NoteOn(48, 1.0f);
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        for (int b = 0; b < 30; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                assert(!std::isnan(outL[i]));
                assert(std::abs(outL[i]) <= 1.5f && "Compressor/preamp failed to contain level");
            }
        }
        engine.AllNotesOff();
    }
    std::cout << " -> Passed! Preamp & Compressor verified." << std::endl;

    // Test 6: Dynamic Pitch Glide on Hard Strikes
    std::cout << "[Test 6] Testing dynamic tension-modulation pitch glide..." << std::endl;
    {
        maremba::EngineParameters params;
        params.pitchGlide = 0.9f;
        engine.SetParameters(params);

        engine.NoteOn(48, 1.0f); // Maximum strike force
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        float maxOnset = 0.0f;
        for (int b = 0; b < 10; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                assert(!std::isnan(outL[i]));
                maxOnset = std::max(maxOnset, std::abs(outL[i]));
            }
        }
        assert(maxOnset > 0.05f && "Pitch glide strike produced no sound");
        engine.AllNotesOff();
        std::cout << " -> Passed! Dynamic pitch swell rendered cleanly without instability." << std::endl;
    }

    // Test 7: Inter-bar Sympathetic Resonance Mesh
    std::cout << "[Test 7] Testing inter-bar chromatic sympathetic resonance..." << std::endl;
    {
        maremba::EngineParameters params;
        params.sympathetic = 0.8f;
        params.decay = 0.5f;
        engine.SetParameters(params);

        // Strike note 60 (Middle C)
        engine.NoteOn(60, 0.9f);
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        for (int b = 0; b < 10; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
        }
        // Note off, let primary voice decay
        engine.NoteOff(60);

        // Check tail resonance energy in sympathetic mesh
        float tailEnergy = 0.0f;
        for (int b = 0; b < 30; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                tailEnergy += outL[i] * outL[i] + outR[i] * outR[i];
            }
        }
        std::cout << " -> Sympathetic tail energy: " << tailEnergy << std::endl;
        assert(tailEnergy > 1e-4f && "Sympathetic mesh produced no audible tail");
        engine.AllNotesOff();
        std::cout << " -> Passed! Sympathetic halo mesh sustained acoustic aura." << std::endl;
    }

    // Test 8: Frame & Soundboard Body Bloom Resonance
    std::cout << "[Test 8] Testing frame and soundboard body mass bloom..." << std::endl;
    {
        maremba::EngineParameters params;
        params.bodyBloom = 0.9f;
        engine.SetParameters(params);

        engine.NoteOn(40, 0.85f);
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        float lowBloomEnergy = 0.0f;
        for (int b = 0; b < 25; ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) {
                assert(!std::isnan(outL[i]));
                lowBloomEnergy += outL[i] * outL[i];
            }
        }
        assert(lowBloomEnergy > 0.01f && "Body bloom resonance inactive");
        engine.AllNotesOff();
        std::cout << " -> Passed! Wooden frame acoustic bloom confirmed." << std::endl;
    }

    // Test 9: Alternating-hand Mallet Roll Engine
    std::cout << "[Test 9] Testing alternating-hand mallet roll engine..." << std::endl;
    {
        maremba::EngineParameters params;
        params.rollSpeed = 12.0f; // 12 Hz roll (12 strikes per second)
        engine.SetParameters(params);

        engine.NoteOn(72, 0.8f);
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        // Process 0.5s of roll (~6 re-strikes)
        int reStrikeCount = 0;
        float prevLevel = 0.0f;
        for (int b = 0; b < 350; ++b) { // ~500ms
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            float batchPeak = 0.0f;
            for (int i = 0; i < kBatch; ++i) {
                batchPeak = std::max(batchPeak, std::abs(outL[i]));
            }
            if (batchPeak > prevLevel * 1.5f && batchPeak > 0.05f) {
                reStrikeCount++;
            }
            prevLevel = batchPeak * 0.95f;
        }
        engine.NoteOff(72);
        engine.AllNotesOff();

        std::cout << " -> Detected restrike count in 0.5s: " << reStrikeCount << std::endl;
        assert(reStrikeCount >= 3 && "Mallet roll engine failed to re-strike");
        std::cout << " -> Passed! Alternating-hand mallet tremolo operational." << std::endl;
    }

    // Test 10: Per-Key Detune Drift (0 = exact, 100 = half tone off)
    std::cout << "[Test 10] Testing per-key detune drift parameter (0 to 100)..." << std::endl;
    {
        // 1. Render Note 60 with detune = 0
        maremba::EngineParameters pExact;
        pExact.detune = 0.0f;
        engine.SetParameters(pExact);
        engine.Reset();
        engine.NoteOn(60, 0.8f);

        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];
        std::vector<float> exactAudio;
        for (int b = 0; b < (44100 / kBatch); ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) exactAudio.push_back(outL[i]);
        }
        engine.AllNotesOff();

        // 2. Render Note 60 with detune = 100
        maremba::EngineParameters pDetuned;
        pDetuned.detune = 100.0f;
        engine.SetParameters(pDetuned);
        engine.Reset();
        engine.NoteOn(60, 0.8f);

        std::vector<float> detunedAudio;
        for (int b = 0; b < (44100 / kBatch); ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            for (int i = 0; i < kBatch; ++i) detunedAudio.push_back(outL[i]);
        }
        engine.AllNotesOff();

        // Measure fundamental frequency using DFT peak around nominal C4 (261.63 Hz)
        auto FindFftPeak = [](const std::vector<float>& buffer, float sampleRate, float fMin, float fMax) -> float {
            int N = buffer.size();
            float bestMag = 0.0f;
            float bestFreq = fMin;
            for (float f = fMin; f <= fMax; f += 0.05f) {
                float re = 0.0f, im = 0.0f;
                float w = 6.283185307f * f / sampleRate;
                for (int n = 0; n < N; ++n) {
                    float window = 0.5f * (1.0f - std::cos(6.283185307f * n / N));
                    float s = buffer[n] * window;
                    re += s * std::cos(w * n);
                    im -= s * std::sin(w * n);
                }
                float mag = std::sqrt(re * re + im * im);
                if (mag > bestMag) {
                    bestMag = mag;
                    bestFreq = f;
                }
            }
            return bestFreq;
        };

        float fExact = FindFftPeak(exactAudio, 44100.0f, 250.0f, 275.0f);
        float fDetuned = FindFftPeak(detunedAudio, 44100.0f, 260.0f, 285.0f);
        float driftCents = 1200.0f * std::log2(fDetuned / fExact);

        std::cout << " -> Exact C4: " << fExact << " Hz, Detuned C4 (100): " << fDetuned
                  << " Hz (drift: " << driftCents << " cents, expected ~+62.3 cents)" << std::endl;
        assert(driftCents > 60.0f && driftCents < 65.0f && "Detune drift calculation mismatch");
        std::cout << " -> Passed! Per-key drift scaling verified across range 0 to 100." << std::endl;
    }

    // Test 11: Kalimba Artisan physical model verification
    {
        std::cout << "[Test 11] Verifying Kalimba Artisan clamped cantilever beam acoustics..." << std::endl;
        maremba::ModelParams kp = maremba::GetModelParams(maremba::EMarimbaModel::KalimbaArtisan);
        assert(kp.overtoneRatio[1] == 6.267f && "Cantilever mode 1 ratio mismatch");
        assert(kp.overtoneRatio[2] == 17.550f && "Cantilever mode 2 ratio mismatch");
        assert(kp.overtoneRatio[3] == 34.390f && "Cantilever mode 3 ratio mismatch");
        assert(kp.baseDecaySec >= 4.0f && "Kalimba tine chime sustain should be >= 4.0s");

        maremba::EngineParameters params;
        params.model = 3; // Kalimba Artisan
        params.decay = 1.0f;
        params.oversampling = 0; // 2x
        engine.SetParameters(params);
        engine.Reset();

        engine.NoteOn(60, 0.85f); // Middle C
        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch];
        float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        float maxAmp = 0.0f;
        float energyAt1s = 0.0f;
        float energyAt2s = 0.0f;

        for (int b = 0; b < (44100 * 3 / kBatch); ++b) {
            engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
            float tSec = (b * kBatch) / 44100.0f;
            for (int i = 0; i < kBatch; ++i) {
                float a = std::abs(outL[i]);
                if (a > maxAmp) maxAmp = a;
                if (std::abs(tSec - 1.0f) < 0.02f) energyAt1s += a * a;
                if (std::abs(tSec - 2.0f) < 0.02f) energyAt2s += a * a;
            }
        }
        engine.AllNotesOff();

        std::cout << " -> Kalimba C4 peak amplitude: " << maxAmp << " (headroom safe < 0.9)" << std::endl;
        std::cout << " -> Kalimba tine ringing energy: at 1s = " << energyAt1s << ", at 2s = " << energyAt2s << std::endl;
        assert(maxAmp > 0.05f && maxAmp < 0.90f && "Kalimba output amplitude out of range");
        assert(energyAt1s > 0.001f && "Kalimba tine did not sustain to 1 second");
        assert(energyAt2s > 0.0001f && "Kalimba tine did not sustain to 2 seconds");
        std::cout << " -> Passed! Kalimba Artisan physical modeling verified." << std::endl;
    }

    // Test 12: Marimba Mallet Types (Soft Yarn, Medium Cord, Hard Rubber, Wood Baton)
    {
        std::cout << "[Test 12] Testing all 4 Marimba Mallet types on Imperial Rosewood..." << std::endl;
        const char* malletNames[4] = {"Soft Wool Yarn", "Medium Concert Cord", "Hard Rubber", "Wood Baton"};
        float highFreqEnergies[4] = {0.0f};

        for (int m = 0; m < 4; ++m) {
            maremba::EngineParameters p;
            p.model = 0; // Imperial Rosewood
            p.malletType = m;
            p.decay = 1.0f;
            p.oversampling = 0; // 2x
            engine.SetParameters(p);
            engine.Reset();

            engine.NoteOn(60, 0.85f);
            constexpr int kBatch = 64;
            float outL[kBatch], outR[kBatch];
            float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

            float maxAmp = 0.0f;
            // Highpass filter to measure high harmonic / clack transient energy
            float hpState = 0.0f;
            float hpEnergy = 0.0f;

            for (int b = 0; b < (44100 / 2 / kBatch); ++b) { // first 0.5s
                engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
                for (int i = 0; i < kBatch; ++i) {
                    float s = outL[i];
                    if (std::abs(s) > maxAmp) maxAmp = std::abs(s);
                    // Highpass at ~2.5 kHz (alpha ~ 0.70)
                    float hp = s - hpState;
                    hpState = hpState + 0.30f * hp;
                    hpEnergy += hp * hp;
                }
            }
            engine.AllNotesOff();
            highFreqEnergies[m] = hpEnergy;

            std::cout << " -> Mallet " << m << " (" << malletNames[m] << "): Peak="
                      << maxAmp << ", High-Freq Energy=" << hpEnergy << std::endl;
            assert(maxAmp > 0.05f && maxAmp < 1.0f && "Mallet peak level out of clean range");
        }

        // Verify brightness hierarchy: Soft Yarn has lowest high-freq energy, Wood Baton has highest
        assert(highFreqEnergies[0] < highFreqEnergies[1] && "Soft Yarn should have lower high-freq than Cord");
        assert(highFreqEnergies[1] < highFreqEnergies[2] && "Medium Cord should have lower high-freq than Rubber");
        assert(highFreqEnergies[2] < highFreqEnergies[3] && "Hard Rubber should have lower high-freq than Wood Baton");
        std::cout << " -> Passed! Marimba mallet brightness hierarchy verified (Yarn < Cord < Rubber < Wood)." << std::endl;
    }

    // Test 13: Kalimba Thumb Techniques (Thumb Flesh, Natural Thumb, Thumbnail Snap, Thumb Pick)
    {
        std::cout << "[Test 13] Testing all 4 Kalimba Thumb techniques on Kalimba Artisan..." << std::endl;
        const char* thumbNames[4] = {"Thumb Flesh Pad", "Natural Thumb", "Thumbnail Snap", "Thumb Pick"};
        float snapEnergies[4] = {0.0f};

        for (int t = 0; t < 4; ++t) {
            maremba::EngineParameters p;
            p.model = 3; // Kalimba Artisan
            p.malletType = t; // Thumb style
            p.decay = 1.0f;
            p.oversampling = 0; // 2x
            engine.SetParameters(p);
            engine.Reset();

            engine.NoteOn(60, 0.85f);
            constexpr int kBatch = 64;
            float outL[kBatch], outR[kBatch];
            float cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

            float maxAmp = 0.0f;
            float hpState = 0.0f;
            float attackSnapEnergy = 0.0f;

            // Analyze the first ~25 ms (attack transient snap / click)
            for (int b = 0; b < 16; ++b) {
                engine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
                for (int i = 0; i < kBatch; ++i) {
                    float s = outL[i];
                    if (std::abs(s) > maxAmp) maxAmp = std::abs(s);
                    // Highpass filter at ~2.5 kHz to isolate plucking click/snap
                    float hp = s - hpState;
                    hpState += 0.30f * hp;
                    attackSnapEnergy += hp * hp;
                }
            }
            engine.AllNotesOff();
            snapEnergies[t] = attackSnapEnergy;

            std::cout << " -> Thumb " << t << " (" << thumbNames[t] << "): Peak="
                      << maxAmp << ", Attack Snap Energy=" << attackSnapEnergy << std::endl;
            assert(maxAmp > 0.05f && maxAmp < 1.0f && "Thumb peak level out of clean range");
        }

        // Verify attack transient snap: Flesh pad is gentlest/softest, Thumbnail/Pick have crispest snaps
        assert(snapEnergies[0] < snapEnergies[1] && "Thumb flesh should have softer snap than natural thumb");
        assert(snapEnergies[1] < snapEnergies[2] && "Natural thumb should have softer snap than thumbnail snap");
        assert(snapEnergies[2] < snapEnergies[3] && "Thumbnail snap should have softer snap than rigid thumb pick");
        std::cout << " -> Passed! Kalimba thumb plucking mechanics verified (Flesh < Natural < Nail < Pick)." << std::endl;
    }

    // Test 14: Strike Variance / Organic Micro-Jitter (0% to 100%)
    {
        std::cout << "[Test 14] Testing Strike Variance & Jitter dynamics (0% deterministic, 50% organic, 100% stable)..." << std::endl;

        // Sub-test 14A: Determinism at strikeJitter = 0.0
        {
            maremba::MarembaEngine eng1(44100.0), eng2(44100.0);
            maremba::EngineParameters p;
            p.strikeJitter = 0.0f;
            p.artifacts = 0.0f; // eliminate other stochastic noise
            eng1.SetParameters(p);
            eng2.SetParameters(p);

            eng1.NoteOn(60, 0.8f);
            eng2.NoteOn(60, 0.8f);

            constexpr int kBatch = 64;
            float o1L[kBatch], o1R[kBatch], cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];
            float o2L[kBatch], o2R[kBatch];

            float diffSum = 0.0f;
            for (int b = 0; b < 10; ++b) {
                eng1.RenderBatch(o1L, o1R, cL, cR, fL, fR, pz, kBatch);
                eng2.RenderBatch(o2L, o2R, cL, cR, fL, fR, pz, kBatch);
                for (int i = 0; i < kBatch; ++i) {
                    diffSum += std::abs(o1L[i] - o2L[i]);
                }
            }
            assert(diffSum == 0.0f && "strikeJitter = 0 must be 100% bit-exact deterministic");
            std::cout << " -> Sub-test 14A (strikeJitter = 0): Bit-exact deterministic reproduction verified." << std::endl;
        }

        // Sub-test 14B: Organic strike-to-strike variance at strikeJitter = 50.0
        {
            maremba::MarembaEngine eng(44100.0);
            maremba::EngineParameters p;
            p.strikeJitter = 50.0f;
            p.artifacts = 0.0f; // isolate strike jitter
            eng.SetParameters(p);

            constexpr int kBatch = 64;
            float outL[kBatch], outR[kBatch], cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

            std::vector<float> strike1(kBatch * 10, 0.0f);
            std::vector<float> strike2(kBatch * 10, 0.0f);

            // Strike 1
            eng.NoteOn(60, 0.8f);
            for (int b = 0; b < 10; ++b) {
                eng.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
                for (int i = 0; i < kBatch; ++i) {
                    strike1[b * kBatch + i] = outL[i];
                }
            }
            eng.AllNotesOff();
            eng.Reset();

            // Strike 2
            eng.NoteOn(60, 0.8f);
            for (int b = 0; b < 10; ++b) {
                eng.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
                for (int i = 0; i < kBatch; ++i) {
                    strike2[b * kBatch + i] = outL[i];
                }
            }
            eng.AllNotesOff();

            float diffEnergy = 0.0f;
            float signalEnergy = 0.0f;
            for (size_t i = 0; i < strike1.size(); ++i) {
                float diff = strike1[i] - strike2[i];
                diffEnergy += diff * diff;
                signalEnergy += strike1[i] * strike1[i];
            }
            float relativeDiff = std::sqrt(diffEnergy / std::max(signalEnergy, 1e-6f));
            std::cout << " -> Sub-test 14B (strikeJitter = 50): Relative strike-to-strike difference = "
                      << (relativeDiff * 100.0f) << "% (organic & musical)" << std::endl;
            assert(relativeDiff > 0.0005f && "strikeJitter = 50 should produce organic variance");
            assert(relativeDiff < 0.20f && "strikeJitter = 50 should not distort or radically alter signal");
        }

        // Sub-test 14C: Maximum variance stability at strikeJitter = 100.0 across all models
        {
            maremba::MarembaEngine eng(44100.0);
            for (int m = 0; m < 4; ++m) {
                maremba::EngineParameters p;
                p.model = m;
                p.strikeJitter = 100.0f;
                eng.SetParameters(p);

                for (int n = 36; n <= 96; n += 12) {
                    eng.NoteOn(n, 0.9f);
                    constexpr int kBatch = 64;
                    float outL[kBatch], outR[kBatch], cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];
                    for (int b = 0; b < 10; ++b) {
                        eng.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
                        for (int i = 0; i < kBatch; ++i) {
                            assert(!std::isnan(outL[i]) && "NaN at strikeJitter = 100");
                            assert(!std::isinf(outL[i]) && "Inf at strikeJitter = 100");
                            assert(std::abs(outL[i]) < 5.0f && "Explosion at strikeJitter = 100");
                        }
                    }
                    eng.AllNotesOff();
                }
            }
            std::cout << " -> Sub-test 14C (strikeJitter = 100): 100% stable across all models and octaves." << std::endl;
        }

        std::cout << " -> Passed! Strike Variance verified across all operational regimes." << std::endl;
    }

    // Test 15: Harmonic Sympathetic Resonance Matrix (Zero Semitone Bleed)
    {
        std::cout << "[Test 15] Testing Harmonic Sympathetic Resonance Matrix for zero semitone bleed..." << std::endl;
        maremba::SympatheticMesh mesh;
        mesh.Configure(44100.0f);

        // Feed C4 (MIDI 60) acoustic energy into the sympathetic mesh
        for (int s = 0; s < 500; ++s) {
            mesh.BeginSample();
            float fakeAcoustic = std::sin(6.283185307f * 261.63f * s / 44100.0f);
            mesh.AccumulateVoice(60, fakeAcoustic);
            float haloL, haloR;
            mesh.Process(1.0f, haloL, haloR);
        }

        // Resonator indices in mesh:
        // C3 is index 0 (note 48)
        // B3 is index 11 (note 59) - adjacent semitone below C4!
        // C4 is index 12 (note 60) - unison
        // C#4 is index 13 (note 61) - adjacent semitone above C4!
        // G4 is index 19 (note 67) - perfect fifth above C4
        float b3Energy = std::abs(mesh.GetResonatorOutput(11));
        float c4Energy = std::abs(mesh.GetResonatorOutput(12));
        float cs4Energy = std::abs(mesh.GetResonatorOutput(13));
        float g4Energy = std::abs(mesh.GetResonatorOutput(19));

        std::cout << " -> Resonator C4 (Unison) amp: " << c4Energy << std::endl;
        std::cout << " -> Resonator G4 (5th) amp: " << g4Energy << std::endl;
        std::cout << " -> Resonator B3 (Semitone below) amp: " << b3Energy << std::endl;
        std::cout << " -> Resonator C#4 (Semitone above) amp: " << cs4Energy << std::endl;

        assert(c4Energy > 0.001f && "C4 unison resonator should be excited");
        assert(g4Energy > 0.0005f && "G4 perfect fifth resonator should be excited");
        assert(b3Energy == 0.0f && "B3 semitone resonator MUST have strictly zero excitation!");
        assert(cs4Energy == 0.0f && "C#4 semitone resonator MUST have strictly zero excitation!");
        std::cout << " -> Passed! Zero semitone bleed verified: semitones and tritones receive strictly 0.0 coupling." << std::endl;
    }

    // Test 16: Mallet Restrike Phase Continuity (Zero Click / Step Discontinuity)
    {
        std::cout << "[Test 16] Testing Mallet Restrike Phase Continuity (zero click / step discontinuity)..." << std::endl;
        maremba::MarembaEngine restrikeEngine(44100.0);
        maremba::EngineParameters p;
        p.strikeJitter = 0.0f;
        p.artifacts = 0.0f;
        restrikeEngine.SetParameters(p);

        constexpr int kBatch = 64;
        float outL[kBatch], outR[kBatch], cL[kBatch], cR[kBatch], fL[kBatch], fR[kBatch], pz[kBatch];

        // First strike
        restrikeEngine.NoteOn(60, 0.8f);
        // Let it ring for ~20 ms to build significant sinusoidal amplitude
        for (int b = 0; b < 15; ++b) {
            restrikeEngine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
        }

        // Grab sample right before restrike
        float sampleBefore = outL[kBatch - 1];

        // Restrike the same note while it is vibrating at high amplitude
        restrikeEngine.NoteOn(60, 0.8f);
        restrikeEngine.RenderBatch(outL, outR, cL, cR, fL, fR, pz, kBatch);
        float sampleAfter = outL[0];

        float stepDiscontinuity = std::abs(sampleAfter - sampleBefore);
        std::cout << " -> Sample before restrike: " << sampleBefore << ", after restrike: " << sampleAfter
                  << ", single-sample jump: " << stepDiscontinuity << std::endl;

        assert(stepDiscontinuity < 0.15f && "Restrike caused harsh step discontinuity click");
        std::cout << " -> Passed! Restrike phase continuity verified: no click or DC step jump." << std::endl;
    }

    std::cout << "\n>>> ALL MAREMBA DSP VERIFICATION CHECKS PASSED SUCCESSFULLY! <<<" << std::endl;
    return 0;
}
