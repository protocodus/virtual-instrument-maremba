#pragma once

#include "AcousticRoom.h"
#include <algorithm>
#include <cmath>

namespace maremba {

class MicMixer {
public:
    MicMixer() {
        Reset();
    }

    void SetSampleRate(float sampleRate) {
        mRoom.SetSampleRate(sampleRate);
    }

    void Reset() {
        mRoom.Reset();
    }

    // Process a sample from a voice at a specific MIDI note (for keyboard panning)
    void MixVoiceSample(int noteNumber, float closeIn, float farIn, float piezoIn,
                        float& accCloseL, float& accCloseR,
                        float& accFarL, float& accFarR,
                        float& accPiezo)
    {
        // Stereo panning across keyboard: C2 (MIDI 36) -> pan 0.15 (left), C7 (MIDI 96) -> pan 0.85 (right)
        float panNorm = std::clamp((static_cast<float>(noteNumber) - 36.0f) / 60.0f, 0.0f, 1.0f);
        float panBipolar = (panNorm - 0.5f) * 2.0f;

        // Apply keyboard pan to Close mic
        float angle = (panBipolar * 0.45f + 0.5f) * 1.57079632679f; // 0 to pi/2
        float gainL = std::cos(angle);
        float gainR = std::sin(angle);

        accCloseL += closeIn * gainL;
        accCloseR += closeIn * gainR;

        // Far/Room mic has diffuse spatialization (wider stereo image, softer panning)
        float farAngle = (panBipolar * 0.25f + 0.5f) * 1.57079632679f;
        accFarL += farIn * std::cos(farAngle);
        accFarR += farIn * std::sin(farAngle);

        // Piezo is direct contact (mono)
        accPiezo += piezoIn;
    }

    // Finalize one audio frame through mic levels and acoustic room space
    void ProcessFrame(float inCloseL, float inCloseR,
                      float inFarL, float inFarR,
                      float inPiezo,
                      float closeLevel, float farLevel, float piezoLevel,
                      float stereoWidth,
                      float& outMainL, float& outMainR,
                      float& outDirectCloseL, float& outDirectCloseR,
                      float& outDirectFarL, float& outDirectFarR,
                      float& outDirectPiezo)
    {
        // 1. Close Mic with Stereo Width (Mid-Side)
        float closeMid = (inCloseL + inCloseR) * 0.5f;
        float closeSide = (inCloseR - inCloseL) * 0.5f * std::clamp(stereoWidth, 0.0f, 2.0f);
        outDirectCloseL = closeMid - closeSide;
        outDirectCloseR = closeMid + closeSide;

        // 2. Far/Room Mic — through authentic wooden concert hall reverberation
        mRoom.Process(inFarL, inFarR, farLevel, outDirectFarL, outDirectFarR);

        // 3. Piezo Contact Pickup — direct
        outDirectPiezo = inPiezo;

        // 4. Combined Master Stereo Outputs (scaled with clean headroom)
        // Strictly zero if all mic levels are turned down
        constexpr float kMasterMixScale = 0.65f;
        outMainL = ((outDirectCloseL * closeLevel) + (outDirectFarL * farLevel) + (outDirectPiezo * piezoLevel * 0.707f)) * kMasterMixScale;
        outMainR = ((outDirectCloseR * closeLevel) + (outDirectFarR * farLevel) + (outDirectPiezo * piezoLevel * 0.707f)) * kMasterMixScale;
    }

private:
    AcousticRoom mRoom;
};

} // namespace maremba
