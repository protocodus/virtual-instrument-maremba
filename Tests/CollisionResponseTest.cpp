// Isolated mechanical-audio regressions: repeated impacts must not take over
// a resonant note, and treble contacts must survive changes in sample rate.
#include "../DSP/MarembaVoice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void DisableContactNoise(MarembaVoice& voice) {
        voice.mClickGain = 0.0;
        voice.mTextureDepth = 0.0;
    }
    // The knock where it goes: a bar's into its microphones, a tine's into
    // the bridge (the box radiates it).
    static double CollisionSample(const MarembaVoice& voice) { return voice.mCollisionOut; }
    static double NoteSample(const MarembaVoice& voice, double close) {
        return voice.mTine ? voice.mBridgeSample - voice.mCollisionOut : close;
    }
    static bool ContactStarted(const MarembaVoice& voice) {
        return voice.mCollisionSample == 1 && voice.mCollisionImpulse != 0.0;
    }
    static bool HasScheduledBrush(const MarembaVoice& voice) { return voice.mBrushImpulse > 0.0; }
    static double BrushFrequency(const MarembaVoice& voice, int mode) {
        return voice.mBrushModes[mode].w * voice.mSampleRate / maremba::kTwoPiD;
    }
    static double BrushEnergy(const MarembaVoice& voice) {
        return MarembaVoice::ModeEnergy(voice.mBrushModes[0]) + MarembaVoice::ModeEnergy(voice.mBrushModes[1]);
    }
};
} // namespace maremba

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
struct Result {
    double collisionEnergy = 0.0, closeEnergy = 0.0;
    double firstContact = -1.0;
    int contacts = 0;
};

Result Render(int model, int note, double rate, int seed, bool stop = false) {
    maremba::MarembaVoice voice;
    maremba::FastPRNG random(static_cast<uint32_t>(seed) * 7919u);
    voice.Trigger(note, .85f, static_cast<maremba::EMarimbaModel>(model), .4f, .35f,
                  0, .7f, 1, 0, 1, 0, rate, random, 0, 0, 1, 0);
    maremba::MarembaVoiceTestAccess::DisableContactNoise(voice);
    Result result;
    const int frames = static_cast<int>(rate * (stop ? 2.0 : 0.18));
    for (int i = 0; i < frames; ++i) {
        // Stop during the original gesture, with possible brushes/rebounds
        // and a fractional mounting contact still pending.
        if (stop && i == static_cast<int>(rate * .003)) voice.StartDamping();
        float close = 0, far = 0, piezo = 0;
        voice.ProcessSample(close, far, piezo);
        Require(std::isfinite(close) && std::isfinite(far) && std::isfinite(piezo),
                "collision and pickup audio must stay finite");
        const double collision = maremba::MarembaVoiceTestAccess::CollisionSample(voice);
        const double note = maremba::MarembaVoiceTestAccess::NoteSample(voice, close);
        result.collisionEnergy += collision * collision / rate;
        result.closeEnergy += note * note / rate;
        if (maremba::MarembaVoiceTestAccess::ContactStarted(voice)) {
            ++result.contacts;
            if (result.firstContact < 0.0) result.firstContact = i / rate;
        }
        if (i % 32 == 0) voice.UpdateEnvelope();
    }
    if (stop) {
        Require(!voice.IsActive() && voice.ComputeEnergy() == 0.0 && voice.HasNoSubnormalState(),
                "stop must drain pending contact energy and clear the complete voice");
    }
    return result;
}

void CheckCleanRestrike(int model) {
    for (int seed = 1; seed <= 64; ++seed) {
        maremba::MarembaVoice voice;
        maremba::FastPRNG random(static_cast<uint32_t>(seed) * 7919u);
        float globalCents = 0.0f;
        const auto trigger = [&](float artifacts) {
            voice.Trigger(60, .85f, static_cast<maremba::EMarimbaModel>(model), .4f, .35f,
                          0, .7f, 1, 0, artifacts, 0, 96000, random, globalCents, 0, 1, 0);
        };
        trigger(1.0f);
        if (!maremba::MarembaVoiceTestAccess::HasScheduledBrush(voice)) continue;
        for (int i = 0; i < 1440; ++i) {
            float close, far, piezo;
            voice.ProcessSample(close, far, piezo);
            if (i % 32 == 0) voice.UpdateEnvelope();
        }
        globalCents = 137.0f;
        voice.Retune(globalCents);
        const double energy = maremba::MarembaVoiceTestAccess::BrushEnergy(voice);
        Require(energy > 1.0e-9, "clean-restrike probe must start with an audible neighboring-key tail");
        const double f0 = maremba::MarembaVoiceTestAccess::BrushFrequency(voice, 0);
        const double f1 = maremba::MarembaVoiceTestAccess::BrushFrequency(voice, 1);
        trigger(0.0f);
        Require(!maremba::MarembaVoiceTestAccess::HasScheduledBrush(voice),
                "clean restrike must not schedule another accidental neighbor contact");
        Require(std::abs(maremba::MarembaVoiceTestAccess::BrushFrequency(voice, 0) - f0) < 1.0e-9
             && std::abs(maremba::MarembaVoiceTestAccess::BrushFrequency(voice, 1) - f1) < 1.0e-9,
                "a clean restrike must not retune the already ringing neighboring key");
        Require(std::abs(maremba::MarembaVoiceTestAccess::BrushEnergy(voice) - energy) < energy * 1.0e-6,
                "a clean restrike must preserve the neighboring key's stored ring");
        // A later parameter notification may repeat the current bend. The new
        // strike's tuning reference must not make the old neighbor jump back.
        voice.Retune(globalCents);
        Require(std::abs(maremba::MarembaVoiceTestAccess::BrushFrequency(voice, 0) - f0) < 1.0e-9
             && std::abs(maremba::MarembaVoiceTestAccess::BrushFrequency(voice, 1) - f1) < 1.0e-9,
                "reapplying the same bend after a clean restrike must preserve neighbor tuning");
        return;
    }
    Require(false, "clean-restrike probe could not find a neighboring-key brush");
}
} // namespace

int main() {
    // C5's third harmonic is close to the Kalimba bridge resonance. An earlier
    // version accumulated coherent half-cycle impacts until they swamped the
    // intended note. Measure sound energy, without asserting its limiter law.
    double maxHotNoteRatio = 0.0;
    int hotContacts = 0;
    for (int seed = 1; seed <= 40; ++seed) {
        const Result result = Render(3, 72, 96000.0, seed);
        if (result.contacts == 0) continue;
        const double rmsRatio = std::sqrt(result.collisionEnergy / std::max(result.closeEnergy, 1.0e-30));
        maxHotNoteRatio = std::max(maxHotNoteRatio, rmsRatio);
        hotContacts += result.contacts;
    }
    Require(hotContacts > 100, "hot-note probe must exercise repeated real mounting contacts");
    Require(maxHotNoteRatio < .30, "coherent bridge collisions must stay subordinate to the intended note");
    std::printf("Kalimba C5 maximum isolated collision / close RMS: %.5f\n", maxHotNoteRatio);

    // Voices always run oversampled (2x at least), so 88.2 kHz is the lowest.
    constexpr std::array<double, 5> rates = {88200, 96000, 176400, 192000, 768000};
    for (int model = 0; model < 4; ++model) {
        CheckCleanRestrike(model);
        std::array<double, 5> energy{};
        std::array<int, 5> contacts{};
        double maxOnsetSeconds = 0.0;
        for (int seed = 1; seed <= 12; ++seed) {
            const Result reference = Render(model, 96, 96000, seed);
            for (size_t r = 0; r < rates.size(); ++r) {
                const Result result = Render(model, 96, rates[r], seed);
                energy[r] += result.collisionEnergy;
                contacts[r] += result.contacts;
                if (reference.firstContact >= 0.0 && result.firstContact >= 0.0) {
                    const double difference = std::abs(reference.firstContact - result.firstContact);
                    maxOnsetSeconds = std::max(maxOnsetSeconds, difference);
                    // A knock is a swing of the bar past its clearance, so the
                    // first one may land on the neighbouring swing when the
                    // clearance is crossed within a sample of the moment the
                    // mount is armed; never later than one C7 period.
                    const double period = 1.0 / (440.0 * std::exp2((96 - 69) / 12.0));
                    Require(difference <= period + 2.0 / rates[r] + 2.0 / 96000.0,
                            "collision onset must remain within one swing and the sample resolution");
                }
            }
        }
        Require(contacts[2] > 100 && energy[2] > 1.0e-6,
                "treble probe must exercise audible collisions at every instrument");
        double maxRateDb = 0.0, maxCountFraction = 0.0;
        for (size_t r = 0; r < rates.size(); ++r) {
            maxRateDb = std::max(maxRateDb, std::abs(10.0 * std::log10(energy[r] / energy[2])));
            maxCountFraction = std::max(maxCountFraction,
                std::abs(static_cast<double>(contacts[r] - contacts[2])) / contacts[2]);
        }
        // Aggregate a seeded series: a nearly missed graze may legitimately
        // lose its last tiny collision at a different sampling phase. A long
        // fixed contact pulse previously dropped many audible treble contacts,
        // changing the whole series by several dB rather than sample rounding.
        Require(maxRateDb < 1.0, "treble collision audio must not change level substantially with sample rate");
        // The balafon's top key grazes its mount a few dozen more times at
        // 88.2 kHz (7 %) with the same collision energy (0.2 dB): tiny grazes
        // at a sampling phase, not dropped knocks.
        Require(maxCountFraction < .08, "sample rate must not systematically drop treble contacts");
        std::printf("model=%d C7 collision rate spread=%.5f dB contact-count spread=%.5f onset=%.3f us\n",
                    model, maxRateDb, maxCountFraction, maxOnsetSeconds * 1.0e6);
        for (int seed : {1, 4, 8, 11}) Render(model, 72, 96000, seed, true);
    }
    std::puts("Collision resonance, treble-rate and stop contracts passed.");
}
