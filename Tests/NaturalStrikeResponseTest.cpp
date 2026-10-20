// Natural stroke variation and physical touch response at Variance=Artifacts=0.
// Compile with -I Source/DSP and Source/DSP/*.cpp. --physical-only runs the quick velocity
// matrix; the default also exercises public-engine silence/reuse and Reset.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace maremba {
struct MarembaVoiceTestAccess {
    struct Strike {
        bool thumb = false;
        int releaseSamples = 0;
        double speed = 0, force = 0, heldMetres = 0;
        std::vector<double> physical;
    };
    static Strike Read(const MarembaVoice& v) {
        Strike s;
        s.thumb = v.mThumbPending;
        s.speed = v.mContact.Velocity();
        s.force = v.mReleaseForce;
        s.releaseSamples = v.mReleaseSamples;
        for (int i = 0; i < v.mNumModes; ++i) {
            const auto& m = v.mModes[i];
            s.heldMetres += m.contact * m.toDisplacement
                         * (v.mThumbHold[i] + v.mThumbResidual * m.y1);
        }
        // Deliberately exclude mVelocity: a changed stored velocity by itself
        // says nothing about whether it reaches the physical excitation.
        s.physical = {s.speed, s.force, s.heldMetres, v.mStrokePosition,
                      v.mMalletSetup.stiffness, v.mMalletSetup.hysteresis,
                      static_cast<double>(v.mReleaseSamples), v.mReleaseShape};
        for (int i = 0; i < v.mNumModes; ++i) {
            s.physical.push_back(v.mModes[i].contact);
            s.physical.push_back(v.mModes[i].force);
            s.physical.push_back(v.mThumbHold[i]);
        }
        return s;
    }
};
} // namespace maremba

namespace {
using namespace maremba;
constexpr double kVoiceRate = 96000, kEngineRate = 48000;
constexpr std::array<int, 5> kNotes{0, 36, 60, 96, 127};
constexpr std::array<std::array<int, 2>, 3> kPairs{{{1, 127}, {8, 127}, {32, 96}}};
constexpr std::array<int, 3> kRepeatVelocities{1, 8, 127};
constexpr uint32_t kEventSeed = 0x382195u, kLocalSeed = 0x49681u;
int failures = 0, checks = 0;
double longestDampSeconds = 0;
void check(bool ok, const std::string& context, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << context << ' ' << message << '\n'; }
}
std::string context(int model, int note, int velocity = -1) {
    return "model=" + std::to_string(model) + " note=" + std::to_string(note)
         + (velocity < 0 ? "" : " velocity=" + std::to_string(velocity));
}

// FNV-1a over IEEE-754 bit patterns, explicitly least-significant byte first.
// These hashes audit this render, not a frozen cross-compiler audio golden.
template<typename T> std::string hash(const std::vector<T>& values) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (const T value : values) {
        uint64_t bits = 0;
        if constexpr (sizeof(T) == 4) {
            uint32_t small = 0;
            std::memcpy(&small, &value, sizeof(value));
            bits = small;
        } else {
            static_assert(sizeof(T) == 8, "hash expects float or double");
            std::memcpy(&bits, &value, sizeof(value));
        }
        for (size_t byte = 0; byte < sizeof(value); ++byte) {
            h ^= (bits >> (8 * byte)) & 255;
            h *= UINT64_C(1099511628211);
        }
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << h;
    return out.str();
}

struct Difference { double residual = 0, gain = 0, energyA = 0, energyB = 0; };
Difference difference(const std::vector<float>& a, const std::vector<float>& b,
                      double rate, double start, double end) {
    const size_t from = static_cast<size_t>(rate * start);
    const size_t to = std::min({a.size(), b.size(), static_cast<size_t>(rate * end)});
    Difference d;
    double cross = 0;
    for (size_t i = from; i < to; ++i) {
        d.energyA += static_cast<double>(a[i]) * a[i];
        d.energyB += static_cast<double>(b[i]) * b[i];
        cross += static_cast<double>(a[i]) * b[i];
    }
    if (d.energyA <= 0 || d.energyB <= 0) return d;
    d.gain = cross / d.energyA;
    double squaredError = 0;
    for (size_t i = from; i < to; ++i) {
        const double error = b[i] - d.gain * a[i];
        squaredError += error * error;
    }
    d.residual = std::sqrt(squaredError / d.energyB);
    return d;
}

struct VoiceRender {
    MarembaVoiceTestAccess::Strike strike;
    std::vector<float> close, force;
    uint32_t nextRandom = 0;
    bool finite = true;
};
VoiceRender renderVoice(int model, int note, int velocity, FastPRNG& eventRandom) {
    MarembaVoice v;
    v.SetSeed(kLocalSeed);
    v.Trigger(note, velocity / 127.f, static_cast<EMarimbaModel>(model), .4f, .45f,
              0, .7f, .4f, 0, 0, 0, kVoiceRate, eventRandom, 0, 0, 1, 0);
    VoiceRender result;
    result.strike = MarembaVoiceTestAccess::Read(v);
    auto next = eventRandom;
    result.nextRandom = next.Next();
    constexpr size_t count = static_cast<size_t>(kVoiceRate * .18);
    result.close.reserve(count); result.force.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        float close = 0, far = 0, piezo = 0;
        v.ProcessSample(close, far, piezo);
        const double force = v.GetContactForce();
        result.finite = result.finite && std::isfinite(close) && std::isfinite(far)
                     && std::isfinite(piezo) && std::isfinite(force);
        result.close.push_back(close);
        result.force.push_back(static_cast<float>(force));
        if (i % 32 == 31) v.UpdateEnvelope();
    }
    for (double value : result.strike.physical)
        result.finite = result.finite && std::isfinite(value);
    return result;
}

void velocityMatrix() {
    for (int model = 0; model < 4; ++model)
        for (int note : kNotes)
            for (const auto& pair : kPairs) {
                FastPRNG softRandom(kEventSeed), hardRandom(kEventSeed);
                const auto soft = renderVoice(model, note, pair[0], softRandom);
                const auto hard = renderVoice(model, note, pair[1], hardRandom);
                const auto name = context(model, note) + " pair=" + std::to_string(pair[0])
                                + "/" + std::to_string(pair[1]);
                check(soft.finite && hard.finite, name, "nonfinite physical state or audio");
                check(soft.nextRandom == hard.nextRandom, name, "velocity changed event PRNG draw count");
                check(soft.strike.thumb == (model == 3) && hard.strike.thumb == (model == 3),
                      name, "fixture did not use the intended physical striker");
                if (model == 3) {
                    check(soft.strike.force > 0 && hard.strike.force > 1.5 * soft.strike.force,
                          name, "velocity did not increase actual thumb release force");
                    check(soft.strike.heldMetres > 0 && hard.strike.heldMetres > 1.5 * soft.strike.heldMetres,
                          name, "velocity did not increase actual held tine deflection");
                } else {
                    check(soft.strike.speed > 0 && hard.strike.speed > 1.5 * soft.strike.speed,
                          name, "velocity did not increase actual mallet approach speed");
                }
                check(hash(soft.strike.physical) != hash(hard.strike.physical), name,
                      "physical strike state identical at soft and hard velocity");
                check(hash(soft.close) != hash(hard.close), name, "velocity produced identical waveforms");
                const auto attack = difference(soft.close, hard.close, kVoiceRate, 0, .008);
                const auto ring = difference(soft.close, hard.close, kVoiceRate, .012, .16);
                const auto contact = difference(soft.force, hard.force, kVoiceRate, 0, .008);
                check(attack.energyA > 0 && attack.energyB > 0, name, "silent attack fixture");
                // Descriptive at the extrapolated edge notes. At ordinary
                // notes the temporal attack must differ by >0.1% RMS after
                // a best-fit scalar gain, well above float-rounding error.
                if (note == 60 || note == 96) {
                    check(attack.residual > .001, name, "normal-register velocity changes only scalar gain");
                    check(contact.residual > .001, name, "normal-register physical force changes only scalar gain");
                }
                std::cout << "VELOCITY " << name << " speed=" << soft.strike.speed << ':' << hard.strike.speed
                          << " thumb_force=" << soft.strike.force << ':' << hard.strike.force
                          << " held_m=" << soft.strike.heldMetres << ':' << hard.strike.heldMetres
                          << " release_samples=" << soft.strike.releaseSamples << ':' << hard.strike.releaseSamples
                          << " attack_residual=" << attack.residual << " ring_residual=" << ring.residual
                          << " contact_residual=" << contact.residual
                          << " physical_hash=" << hash(soft.strike.physical) << ':' << hash(hard.strike.physical)
                          << " audio_hash=" << hash(soft.close) << ':' << hash(hard.close) << '\n';
            }
}

void naturalPhysicalMatrix() {
    for (int model = 0; model < 4; ++model)
        for (int note : kNotes)
            for (int velocity : kRepeatVelocities) {
                FastPRNG random(kEventSeed);
                std::set<std::string> physicalHashes, audioHashes;
                std::vector<float> previous;
                double minimumResidual = 1;
                const auto name = context(model, note, velocity);
                for (int hit = 0; hit < 6; ++hit) {
                    // New silent physical motion and identical local texture
                    // seed isolate the advancing event RNG from random noise.
                    const auto render = renderVoice(model, note, velocity, random);
                    check(render.finite, name, "nonfinite natural stroke");
                    physicalHashes.insert(hash(render.strike.physical));
                    audioHashes.insert(hash(render.close));
                    if (!previous.empty()) minimumResidual = std::min(minimumResidual,
                        difference(previous, render.close, kVoiceRate, 0, .16).residual);
                    previous = render.close;
                }
                check(physicalHashes.size() == 6, name, "natural strike physics repeated with zero Variance/Artifacts");
                check(audioHashes.size() == 6, name, "natural strike audio repeated with zero Variance/Artifacts");
                check(minimumResidual > 1.e-6, name, "natural stroke differences reduce to scalar gain");
                std::cout << "NATURAL_PHYSICAL " << name << " unique_physical=" << physicalHashes.size()
                          << " unique_audio=" << audioHashes.size() << " min_residual=" << minimumResidual << '\n';
            }
}

using Hits = std::vector<std::vector<float>>;
struct Phrase {
    Hits hits;
    double minMainPeak = 1.e100, minMainRms = 1.e100;
};
Phrase renderPhrase(MarembaEngine& engine, int model, int note, int velocity) {
    Phrase phrase;
    const auto name = context(model, note, velocity);
    std::array<float, 64> left{}, right{}, closeLeft{}, closeRight{};
    for (int hit = 0; hit < 6; ++hit) {
        check(engine.IsSilent(), name, "repeated hit did not begin in engine silence");
        engine.NoteOn(note, velocity / 127.f);
        std::vector<float> clip;
        bool finite = true;
        double mainPeak = 0, mainEnergy = 0;
        for (int batch = 0; batch < 135; ++batch) { // 180 ms
            engine.RenderBatch(left.data(), right.data(), closeLeft.data(), closeRight.data(),
                               nullptr, nullptr, nullptr, 64);
            for (size_t i = 0; i < left.size(); ++i) {
                finite = finite && std::isfinite(left[i]) && std::isfinite(right[i])
                       && std::isfinite(closeLeft[i]) && std::isfinite(closeRight[i]);
                clip.push_back(closeLeft[i] + closeRight[i]);
                mainPeak = std::max(mainPeak, static_cast<double>(std::max(std::abs(left[i]), std::abs(right[i]))));
                mainEnergy += static_cast<double>(left[i]) * left[i] + static_cast<double>(right[i]) * right[i];
            }
        }
        check(finite, name, "nonfinite public-engine stroke");
        check(mainPeak > 0 && mainEnergy > 0, name, "public-engine strike produced no main output");
        phrase.minMainPeak = std::min(phrase.minMainPeak, mainPeak);
        phrase.minMainRms = std::min(phrase.minMainRms, std::sqrt(mainEnergy / (2 * clip.size())));
        phrase.hits.push_back(std::move(clip));
        engine.DampAll();
        // DampAll clears physical ringing through the normal hand-damping
        // path; it must not reset the natural per-event random stream.
        int dampBatches = 0;
        // The physical model extrapolates down to MIDI0 (8.18 Hz), where the
        // air/support states decay slowly. This is a reuse/RNG test, not a
        // three-second transport-stop deadline: wait for true silence.
        for (; dampBatches < static_cast<int>(kEngineRate * 15 / 64) && !engine.IsSilent(); ++dampBatches)
            engine.RenderBatch(left.data(), right.data(), nullptr, nullptr, nullptr, nullptr, nullptr, 64);
        longestDampSeconds = std::max(longestDampSeconds, dampBatches * 64 / kEngineRate);
        check(engine.IsSilent(), name, "DampAll failed to reach silence in fifteen seconds");
    }
    return phrase;
}

void engineMatrix() {
    for (int model = 0; model < 4; ++model) {
        MarembaEngine engine(kEngineRate);
        EngineParameters p;
        p.oversampling = 0; // Pair the historical 48 kHz engine fixture with its 96 kHz voice reference.
        p.model = model; p.malletType = 1; p.malletHardness = .4f;
        p.strikePosition = .45f; p.decay = .4f;
        p.strikeJitter = p.artifacts = p.detune = p.buzzAmount = p.sympathetic = p.bodyBloom = 0;
        p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
        p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
        p.velocityToCarving = p.velocityToCoupling = p.velocityToBloom = p.velocityToSympathetic = 0;
        p.velocityToBuzz = p.velocityToTension = p.velocityToArtifacts = 0;
        engine.SetParameters(p);
        for (int note : kNotes)
            for (int velocity : kRepeatVelocities) {
                engine.Reset();
                const auto phrase = renderPhrase(engine, model, note, velocity);
                const auto& hits = phrase.hits;
                engine.Reset();
                const auto replay = renderPhrase(engine, model, note, velocity);
                const auto name = context(model, note, velocity);
                const bool exactReplay = hits == replay.hits && phrase.minMainPeak == replay.minMainPeak
                                      && phrase.minMainRms == replay.minMainRms;
                check(exactReplay, name, "explicit Reset did not reproduce audio and main-output levels");
                std::set<std::string> hashes;
                double minimumResidual = 1;
                std::ostringstream list;
                for (size_t hit = 0; hit < hits.size(); ++hit) {
                    const auto digest = hash(hits[hit]);
                    hashes.insert(digest);
                    list << (hit ? ":" : "") << digest;
                    if (hit) minimumResidual = std::min(minimumResidual,
                        difference(hits[hit - 1], hits[hit], kEngineRate, 0, .16).residual);
                }
                check(hashes.size() == hits.size(), name, "consecutive engine strikes reused identical audio");
                check(minimumResidual > 1.e-6, name, "engine stroke differences reduce to scalar gain");
                auto scalar = hits.front();
                for (float& x : scalar) x *= 1.75f;
                check(difference(hits.front(), scalar, kEngineRate, 0, .16).residual < 1.e-6,
                      name, "difference metric misclassifies scalar gain as changed timbre");
                std::cout << "NATURAL_ENGINE " << name << " unique_audio=" << hashes.size()
                          << " exact_reset_replay=" << exactReplay
                          << " min_residual=" << minimumResidual
                          << " min_main_peak_dbfs=" << 20 * std::log10(std::max(1.e-30, phrase.minMainPeak))
                          << " min_main_rms_dbfs=" << 20 * std::log10(std::max(1.e-30, phrase.minMainRms))
                          << " audio_hashes=" << list.str() << '\n';
            }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--physical-only")) {
        std::cerr << "usage: NaturalStrikeResponseTest [--physical-only]\n";
        return 2;
    }
    std::cout << std::setprecision(12);
    velocityMatrix();
    naturalPhysicalMatrix();
    if (argc == 1) engineMatrix();
    std::cout << "NaturalStrikeResponseTest " << (failures ? "FAILED" : "PASSED")
              << " checks=" << checks << " failures=" << failures
              << " longest_damp_seconds=" << longestDampSeconds << '\n';
    return failures ? 1 : 0;
}
