// A fixed-force thumb must not bend an extrapolated subaudio tine by metres.
// Check the resulting physical held displacement, velocity response and real
// restrikes, including the old ring retained when the thumb lands again.
#include "MarembaVoice.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static double HeldDisplacement(const MarembaVoice& voice) {
        double displacement = 0;
        for (int m = 0; m < voice.mNumModes; ++m) {
            const auto& mode = voice.mModes[m];
            displacement += mode.contact * mode.toDisplacement
                          * (voice.mThumbHold[m] + voice.mThumbResidual * mode.y1);
        }
        return displacement;
    }
    static bool Pending(const MarembaVoice& voice) { return voice.mThumbPending; }
};
} // namespace maremba

namespace {
using namespace maremba;
int checks = 0;
double largestTravel = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
void checkTravel(const MarembaVoice& voice) {
    require(MarembaVoiceTestAccess::Pending(voice), "fixture did not produce a thumb pluck");
    const double displacement = MarembaVoiceTestAccess::HeldDisplacement(voice);
    largestTravel = std::max(largestTravel, std::abs(displacement));
    // 8 mm is a voiced engineering bound, not a measured specimen dimension.
    require(std::isfinite(displacement) && std::abs(displacement) <= .00800000001,
            "thumb plus retained old motion exceeded 8 mm travel");
}
void strike(MarembaVoice& voice, FastPRNG& random, double rate, int note,
            int striker, float velocity, float cents, float carving,
            bool dead, float variance = 100, float artifacts = 1) {
    StrikeOptions options;
    options.carving = carving;
    options.membraneTension = 0;
    options.deadStroke = dead;
    voice.Trigger(note, velocity, EMarimbaModel::KalimbaArtisan, 1, 1,
                  -50, 1, 8, 1, artifacts, 0, rate, random, cents, -100,
                  striker, variance, 0, options);
    checkTravel(voice);
}
void advance(MarembaVoice& voice, double rate, double seconds) {
    float close, room, piezo;
    for (int sample = 0; sample < static_cast<int>(rate * seconds); ++sample) {
        voice.ProcessSample(close, room, piezo);
        if ((sample & 63) == 0) voice.UpdateEnvelope();
        require(std::isfinite(close) && std::isfinite(room) && std::isfinite(piezo),
                "restrike/finger-return produced nonfinite audio");
    }
}
} // namespace

int main() {
    using namespace maremba;
    int plucks = 0;
    // Sample the complete playable pitch range, all four thumb techniques,
    // both Carving endpoints, maximal touch scatter/brush redistribution and
    // supported master-tune plus hostile pitch-bend endpoints. Internal rates
    // reach the 192 kHz host / 8x quality case without rendering long tails.
    for (double rate : {44100., 96000., 1536000.})
        for (int note : {0, 11, 12, 24, 36, 48, 60, 84, 108, 127})
            for (int striker = 0; striker < 4; ++striker)
                for (float carving : {0.f, 1.f})
                    for (float cents : {-1400.f, 1400.f}) {
                        MarembaVoice voice;
                        FastPRNG random(0x5190u + static_cast<unsigned>(plucks));
                        strike(voice, random, rate, note, striker, 1, cents, carving, false);
                        ++plucks;
                    }
    // Different random draws must never push a capped release beyond its
    // engineering bound, even when character.velocityScale exceeds one.
    for (unsigned seed = 10000; seed < 10256; ++seed)
        for (int striker = 0; striker < 4; ++striker) {
            MarembaVoice voice;
            FastPRNG random(seed);
            strike(voice, random, 96000, 0, striker, 1, -400, 1, false);
            ++plucks;
        }
    // The bass travel limit must not flatten MIDI velocity. Use identical
    // random draws at each velocity and suppress optional stroke artifacts.
    for (int striker = 0; striker < 4; ++striker) {
        double previous = 0;
        for (float velocity : {.125f, .25f, .5f, 1.f}) {
            MarembaVoice voice;
            FastPRNG random(731);
            strike(voice, random, 96000, 0, striker, velocity, -400, 1, false, 0, 0);
            const double displacement = MarembaVoiceTestAccess::HeldDisplacement(voice);
            require(displacement > 1.8 * previous, "bounded bass pluck lost velocity response");
            previous = displacement;
            ++plucks;
        }
    }
    // Return the thumb after 30 ms (Dead) or retain a free ring, then hit the
    // same voice repeatedly. Check the sum of old and new deflection, not
    // merely the new static excitation, after bending the sounding tine.
    for (double rate : {44100., 384000.})
        for (int note : {0, 12, 48, 84})
            for (int striker = 0; striker < 4; ++striker)
                for (bool dead : {false, true}) {
                    MarembaVoice voice;
                    FastPRNG random(8912u + note);
                    for (int hit = 0; hit < 4; ++hit) {
                        strike(voice, random, rate, note, striker, 1, -400,
                               hit & 1 ? 0 : 1, dead);
                        ++plucks;
                        voice.Retune(hit & 1 ? -1400 : 1400);
                        advance(voice, rate, .04);
                    }
                }
    std::cout << "KalimbaExcitationBoundsTest passed: " << plucks
              << " plucks, max held travel " << largestTravel
              << " m, " << checks << " checks\n";
}
