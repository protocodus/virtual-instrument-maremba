#pragma once

#include "KalimbaBody.h"
#include "RoomAlgorithms.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace maremba {

class MicMixer {
public:
    // Master mix scale of the three mic buses (also applied to the direct outs).
    static constexpr float kMasterMixScale = 0.65f;

    MicMixer() {
        // Keyboard geometry is fixed. Evaluate the four trigonometric gains
        // once, not for every voice at every oversampled audio frame.
        for (int note = 0; note < 128; ++note) {
            const float position = std::clamp((static_cast<float>(note) - 36.0f) / 60.0f, 0.0f, 1.0f);
            const float bipolar = (position - 0.5f) * 2.0f;
            const float closeAngle = (bipolar * 0.45f + 0.5f) * 1.57079632679f;
            const float farAngle = (bipolar * 0.25f + 0.5f) * 1.57079632679f;
            mPan[note] = {std::cos(closeAngle), std::sin(closeAngle),
                          std::cos(farAngle), std::sin(farAngle)};
            // A kalimba's tines lie across its bridge low in the middle,
            // rising outward to either side (KalimbaTineSide).
            const float side = static_cast<float>(KalimbaTineSide(note));
            const float tineClose = (side * 0.40f + 0.5f) * 1.57079632679f;
            const float tineFar = (side * 0.25f + 0.5f) * 1.57079632679f;
            mTinePan[note] = {std::cos(tineClose), std::sin(tineClose),
                              std::cos(tineFar), std::sin(tineFar)};
        }
    }

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
    void SetRoomType(int type) { mRoom.SetType(type); }

    // Process a sample from a voice at a specific MIDI note: placed by the
    // keyboard for a bar, by the bridge layout for a tine.
    void MixVoiceSample(int noteNumber, bool tine, float closeIn, float farIn, float piezoIn,
                        float& accCloseL, float& accCloseR,
                        float& accFarL, float& accFarR,
                        float& accPiezo)
    {
        const auto& pan = (tine ? mTinePan : mPan)[std::clamp(noteNumber, 0, 127)];
        accCloseL += closeIn * pan.closeL;
        accCloseR += closeIn * pan.closeR;
        // Far microphones use the same softer keyboard panning as before.
        accFarL += farIn * pan.farL;
        accFarR += farIn * pan.farR;

        // Piezo is direct contact (mono)
        accPiezo += piezoIn;
    }

    // Finalize one audio frame through mic levels and acoustic room space.
    // The close and piezo taps are raw (before their level knobs, no width). The
    // far tap is the far mic before its knob plus the room return, whose level
    // follows the Far (ROOM) knob (roomLevel = farLevel; the room is off at 0).
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

        // 2. Far/Room Mic — through the selected Wood, Chamber or Hall algorithm
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
    struct Pan { float closeL, closeR, farL, farR; };
    std::array<Pan, 128> mPan{}, mTinePan{};
    RoomBank mRoom;
};

} // namespace maremba
