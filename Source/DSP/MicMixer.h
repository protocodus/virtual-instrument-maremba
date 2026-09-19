#pragma once

#include "AcousticRoom.h"
#include <algorithm>
#include <cmath>

namespace maremba {

class MicMixer {
public:
    // Master mix scale of the three mic buses (also applied to the direct outs).
    static constexpr float kMasterMixScale = 0.65f;

    MicMixer() = default;

    // Not realtime safe: reserve the room's delay memory (see AcousticRoom::Allocate).
    void Allocate(double maxSampleRate) {
        mRoom.Allocate(maxSampleRate);
    }

    // Realtime safe: changes the room's active delay lengths (clears the tail).
    void SetSampleRate(double sampleRate) {
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

    // Finalize one audio frame through mic levels and acoustic room space.
    // The direct taps are raw (pre-fader, no width); the far tap includes the room.
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
        // 1. Close Mic — direct
        outDirectCloseL = inCloseL;
        outDirectCloseR = inCloseR;

        // 2. Far/Room Mic — through authentic wooden concert hall reverberation
        float farDryL = 0.0f, farDryR = 0.0f, roomWetL = 0.0f, roomWetR = 0.0f;
        mRoom.Process(inFarL, inFarR, farLevel, farDryL, farDryR, roomWetL, roomWetR);
        outDirectFarL = farDryL + roomWetL;
        outDirectFarR = farDryR + roomWetR;

        // 3. Piezo Contact Pickup — direct
        outDirectPiezo = inPiezo;

        // 4. Combined Master Stereo Outputs (scaled with clean headroom)
        // Strictly zero if all mic levels are turned down
        float dryL = ((outDirectCloseL * closeLevel) + (farDryL * farLevel) + (outDirectPiezo * piezoLevel * 0.707f)) * kMasterMixScale;
        float dryR = ((outDirectCloseR * closeLevel) + (farDryR * farLevel) + (outDirectPiezo * piezoLevel * 0.707f)) * kMasterMixScale;
        float wetL = roomWetL * farLevel * kMasterMixScale;
        float wetR = roomWetR * farLevel * kMasterMixScale;

        // 5. Stereo Width (Mid-Side) on the main bus: 0 = mono, 1 = natural, 2 = wide.
        // The decorrelated room return is never widened past natural (side gain
        // min(width, 1)), so wide settings do not turn it side-heavy.
        const float width = std::clamp(stereoWidth, 0.0f, 2.0f);
        const float mid = (dryL + dryR + wetL + wetR) * 0.5f;
        const float side = (dryR - dryL) * 0.5f * width + (wetR - wetL) * 0.5f * std::min(width, 1.0f);
        outMainL = mid - side;
        outMainR = mid + side;
    }

private:
    AcousticRoom mRoom;
};

} // namespace maremba
