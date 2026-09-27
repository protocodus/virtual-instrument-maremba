// Contact-pickup contracts through the complete engine, including the shared
// Kalimba plate. A voice-only comparison cannot detect a bypass around that
// plate's sensor/cable/input circuit.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static double Clock(const MarembaEngine& e) { return e.mClock; }
    static double PickupEnergy(const MarembaEngine& e) { return e.mKalimbaPickup.StateEnergy(); }
    static bool PickupNormal(const MarembaEngine& e) { return e.mKalimbaPickup.HasNoSubnormalState(); }
    // Stop the sources after a real pluck, leaving only the already charged
    // sensor. Clear the output resamplers too: their delay cannot fake a tail.
    static void LeaveOnlyPickup(MarembaEngine& e) {
        for (auto& voice : e.mVoices) voice.FastKill();
        e.mKalimbaBody.Reset();
        e.mKalimbaBodyRunning = false;
        e.mMicMixer.Reset(); // the independent Room capture otherwise retains its acoustic tail
        e.mPiezoDecimator.Reset();
        e.mOutputHeadroom.Reset(); // queued source audio must not fake a pickup tail
    }
};
} // namespace maremba

namespace {
using namespace maremba;
using Access = MarembaEngineTestAccess;
using Audio = std::array<std::vector<float>, 7>;
int checks = 0, comparisons = 0, isolationCases = 0, lifecycleCases = 0;
double minimumResidual = 1, maximumPeak = 0, longestPickupDrain = 0;
std::string context;

void require(bool okay, const char* message) {
    ++checks;
    if (!okay) {
        std::cerr << "FAIL " << context << ": " << message << '\n';
        std::exit(1);
    }
}

EngineParameters parameters(int model, int quality = 0) {
    EngineParameters p;
    p.model = model; p.oversampling = quality;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.sympathetic = p.bodyBloom = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
    p.closeLevel = p.volume = 1;
    p.strikePosition = .35f;
    return p;
}

Audio render(MarembaEngine& e, int frames, int batch = 64) {
    Audio a;
    for (auto& channel : a) channel.resize(static_cast<size_t>(frames));
    for (int start = 0; start < frames; start += batch) {
        const int n = std::min(batch, frames - start);
        const double clock = Access::Clock(e);
        const bool active = !e.IsSilent();
        e.RenderBatch(a[0].data() + start, a[1].data() + start,
                      a[2].data() + start, a[3].data() + start,
                      a[4].data() + start, a[5].data() + start,
                      a[6].data() + start, n);
        require(!active || Access::Clock(e) > clock,
                "last-resort output guard reset the engine");
    }
    for (const auto& channel : a) for (float value : channel) {
        require(std::isfinite(value), "nonfinite audio");
        maximumPeak = std::max(maximumPeak, std::abs(static_cast<double>(value)));
    }
    return a;
}

Audio strike(double rate, const EngineParameters& p, int note, float velocity = .85f) {
    auto engine = std::make_unique<MarembaEngine>(rate);
    engine->SetParameters(p);
    engine->Reset();
    engine->NoteOn(note, velocity);
    return render(*engine, static_cast<int>(rate * .22));
}

struct Metrics { double residual, relativeDb, closeHigh, pickupHigh; };
Metrics metrics(const Audio& a, double rate, double from, double to) {
    double cc = 0, pp = 0, cp = 0, closeHigh = 0, pickupHigh = 0;
    double closeLow = 0, pickupLow = 0;
    const double alpha = -std::expm1(-kTwoPiD * 1500 / rate);
    const size_t first = static_cast<size_t>(from * rate);
    const size_t last = std::min(a[6].size(), static_cast<size_t>(to * rate));
    for (size_t i = 0; i < last; ++i) {
        const double c = static_cast<double>(a[2][i]) + a[3][i], p = a[6][i];
        closeLow += alpha * (c - closeLow);
        pickupLow += alpha * (p - pickupLow);
        if (i < first) continue;
        cc += c * c; pp += p * p; cp += c * p;
        closeHigh += (c - closeLow) * (c - closeLow);
        pickupHigh += (p - pickupLow) * (p - pickupLow);
    }
    require(cc > 1e-12 && pp > 1e-12, "comparison contains an inaudible capture");
    return {std::max(0., 1 - cp * cp / (cc * pp)), 10 * std::log10(pp / cc),
            10 * std::log10(std::max(closeHigh / cc, 1e-30)),
            10 * std::log10(std::max(pickupHigh / pp, 1e-30))};
}

void audioComparisons() {
    // Include the lowest and highest supported internal rates. Spectral
    // differences are reported, not forced to be brighter at every key:
    // physical strain nodes and an almost pure fundamental can reverse that.
    struct Setup { double rate; int quality; };
    for (const Setup setup : {Setup{22050, 0}, Setup{48000, 1}, Setup{192000, 2}})
        for (int model = 0; model < 4; ++model)
            for (int note : {48, 60, 84}) {
                context = "capture model=" + std::to_string(model) + " note=" + std::to_string(note)
                    + " rate=" + std::to_string(setup.rate) + " quality=" + std::to_string(setup.quality);
                const auto a = strike(setup.rate, parameters(model, setup.quality), note);
                const auto whole = metrics(a, setup.rate, 0, .2);
                const auto attack = metrics(a, setup.rate, 0, .02);
                const auto ring = metrics(a, setup.rate, .02, .2);
                // Only reject an actual gain-scaled duplicate. This threshold
                // is far below the existing voice pickup contract's 1%.
                require(whole.residual > 1e-4, "contact pickup is a gain-scaled copy of close microphone");
                minimumResidual = std::min(minimumResidual, whole.residual);
                ++comparisons;
                std::cout << "CAPTURE model=" << model << " note=" << note << " rate=" << setup.rate
                          << " quality=" << (2 << setup.quality)
                          << " residual=" << whole.residual << " relative_db=" << whole.relativeDb
                          << " attack_residual=" << attack.residual << " ring_residual=" << ring.residual
                          << " attack_high_delta_db=" << attack.pickupHigh - attack.closeHigh
                          << " ring_high_delta_db=" << ring.pickupHigh - ring.closeHigh << '\n';
            }

    // The capture-level knob belongs to the main mixer. In particular, muting
    // it must neither alter acoustic mechanics nor stop the raw pickup socket.
    for (int model = 0; model < 4; ++model) {
        context = "capture isolation model=" + std::to_string(model);
        auto p = parameters(model);
        const auto muted = strike(48000, p, 60);
        p.piezoLevel = 1;
        const auto audible = strike(48000, p, 60);
        for (size_t channel = 2; channel < audible.size(); ++channel)
            require(muted[channel] == audible[channel], "Piezo level alters pre-fader direct capture");
        require(muted[0] != audible[0] || muted[1] != audible[1], "Piezo level does not reach main output");
        ++isolationCases;
    }
}

void append(Audio& destination, const Audio& source) {
    for (size_t c = 0; c < destination.size(); ++c)
        destination[c].insert(destination[c].end(), source[c].begin(), source[c].end());
}

Audio performance(MarembaEngine& e, int batch) {
    auto p = parameters(3);
    p.piezoLevel = 1;
    p.compAmount = 1;
    p.polyphony = 2;
    e.SetParameters(p);
    e.Reset();
    Audio a;
    for (int note : {48, 55, 60, 67, 72, 79}) e.NoteOn(note, 1);
    append(a, render(e, 2048, batch));
    p.model = 0;
    e.SetParameters(p);
    e.NoteOn(36, 1);
    append(a, render(e, 2048, batch));
    p.oversampling = 2;
    e.SetParameters(p);
    e.NoteOn(96, 1); // queued while the quality fade is pending
    append(a, render(e, 2048, batch));
    p.model = 3;
    e.SetParameters(p);
    e.NoteOn(60, .3f);
    append(a, render(e, 2048, batch));
    e.DampAll();
    append(a, render(e, 4096, batch));
    for (size_t c = 0; c < 2; ++c) for (float value : a[c])
        require(std::abs(value) <= .940002f, "main output exceeds its headroom ceiling");
    return a;
}

void determinism() {
    context = "mixed-model quality-switch performance";
    auto a = std::make_unique<MarembaEngine>(48000);
    auto b = std::make_unique<MarembaEngine>(48000);
    const auto first = performance(*a, 64);
    require(first == performance(*b, 7), "pickup performance depends on render batch size");
    require(first == performance(*a, 31), "Reset does not reproduce the complete pickup performance");
    ++lifecycleCases;
}

void sharedPickupTail() {
    struct Setup { double rate; int quality; };
    for (const Setup setup : {Setup{22050, 0}, Setup{48000, 1}, Setup{192000, 2}})
        for (bool changeModel : {false, true}) {
            context = "plate-only tail rate=" + std::to_string(setup.rate)
                + " quality=" + std::to_string(setup.quality) + " model_change=" + std::to_string(changeModel);
            auto e = std::make_unique<MarembaEngine>(setup.rate);
            auto p = parameters(3, setup.quality);
            e->SetParameters(p);
            e->Reset();
            e->NoteOn(60, 1);
            render(*e, static_cast<int>(setup.rate * .037));
            require(Access::PickupEnergy(*e) > 1e-15, "real Kalimba plate does not charge shared pickup");
            Access::LeaveOnlyPickup(*e);
            require(e->GetActiveVoiceCount() == 0, "tail fixture still contains a voice");
            if (changeModel) { p.model = 0; e->SetParameters(p); }
            const int firstFrames = OutputHeadroom::LatencyFrames(setup.rate) + 64;
            const auto tail = render(*e, firstFrames);
            double piezoEnergy = 0, acousticEnergy = 0;
            for (float value : tail[6]) piezoEnergy += value * value;
            for (size_t c = 2; c < 6; ++c) for (float value : tail[c]) acousticEnergy += value * value;
            require(piezoEnergy > 1e-18, "charged plate pickup is cut off after last voice/source ends");
            require(acousticEnergy == 0, "plate pickup state leaks into acoustic captures");
            int frames = firstFrames;
            while (!e->IsSilent() && frames < static_cast<int>(setup.rate * 2)) {
                render(*e, 64);
                frames += 64;
                require(Access::PickupNormal(*e), "shared plate pickup retains subnormal state");
            }
            require(e->IsSilent() && Access::PickupEnergy(*e) == 0,
                    "source-free shared pickup never drains and flushes");
            longestPickupDrain = std::max(longestPickupDrain, frames / setup.rate);
            e->NoteOn(60, 1);
            render(*e, static_cast<int>(setup.rate * .025));
            e->Reset();
            require(e->IsSilent() && Access::PickupEnergy(*e) == 0, "Reset retains shared sensor charge");
            for (const auto& channel : render(*e, 64)) for (float value : channel)
                require(value == 0, "Reset leaks sensor charge to output");
            std::cout << "TAIL rate=" << setup.rate << " quality=" << (2 << setup.quality)
                      << " model_change=" << changeModel << " first_audible_block_energy=" << piezoEnergy
                      << " drain_seconds=" << frames / setup.rate << '\n';
            ++lifecycleCases;
        }
}
} // namespace

int main() {
    std::cout << std::setprecision(10);
    audioComparisons();
    determinism();
    sharedPickupTail();
    std::cout << "Piezo engine contracts passed: comparisons=" << comparisons
              << " isolation_cases=" << isolationCases << " lifecycle_cases=" << lifecycleCases
              << " checks=" << checks << " minimum_gain_fit_residual=" << minimumResidual
              << " maximum_observed_peak=" << maximumPeak
              << " longest_pickup_drain_seconds=" << longestPickupDrain << '\n';
}
