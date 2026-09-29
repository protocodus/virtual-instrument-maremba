// Audible keyboard coverage and quiet restrike continuity, complemented by a
// dense corner roll that must drain after transport stop. No timing benchmark.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <memory>

namespace maremba {
struct MarembaEngineTestAccess {
    static MarembaVoice FirstVoice(const MarembaEngine& engine) {
        for (const auto& voice : engine.mVoices) if (voice.IsActive()) return voice;
        return {};
    }
    static SympatheticMesh Mesh(const MarembaEngine& engine) { return engine.mSympathetic; }
    static KalimbaBody Board(const MarembaEngine& engine) { return engine.mKalimbaBody; }
};
}

namespace {
constexpr double rate = 48000;
double db(double ratio) { return 10 * std::log10(std::max(ratio, 1.e-30)); }
// The frozen pre-fix bank did not publish its first key. This fallback permits
// the SAME regression source to demonstrate the old self-receiver defect.
template<class Mesh> constexpr auto firstKey(int) -> decltype(Mesh::kFirstNote) { return Mesh::kFirstNote; }
template<class Mesh> constexpr int firstKey(...) { return 48; }
maremba::EngineParameters clean(int model) {
    maremba::EngineParameters p;
    p.model = model;
    p.strikePosition = .35f;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.buzzAmount = p.bodyBloom = p.sympathetic = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
    p.closeLevel = p.volume = 1;
    return p;
}
struct Halo { double ratio = 0, sourceRatio = 0, sceneResidual = 0; bool pickupIdentical = true, finite = true; };
Halo haloResponse(int model, int note) {
    std::array<maremba::MarembaEngine, 2> engine{maremba::MarembaEngine(rate), maremba::MarembaEngine(rate)};
    auto p = clean(model);
    for (size_t k = 0; k < engine.size(); ++k) {
        p.sympathetic = static_cast<float>(k);
        engine[k].SetParameters(p);
        engine[k].NoteOn(note, .85f);
    }
    std::array<std::array<float, 64>, 2> left{}, right{}, piezo{};
    std::array<float, 64> mainL{}, mainR{};
    double dry = 0, wet = 0;
    // Independent source oracle retains the prior calibration at a common
    // reference, before microphone geometry. Nearby receivers can legitimately
    // dominate a distant struck key at the listener; that is not source gain.
    auto voice = maremba::MarembaEngineTestAccess::FirstVoice(engine[0]);
    auto mesh = maremba::MarembaEngineTestAccess::Mesh(engine[0]);
    auto board = maremba::MarembaEngineTestAccess::Board(engine[0]);
    auto scene = std::make_unique<maremba::CaptureScene>();
    scene->Allocate(rate);
    const bool tine = model == 3;
    const float position = std::clamp((note - 36.f) / 60.f, 0.f, 1.f);
    const float bipolar = tine ? static_cast<float>(maremba::KalimbaTineSide(note)) : (position - .5f) * 2;
    const double angle = (bipolar * (tine ? .40f : .45f) + .5f) * 1.57079632679f;
    const double oldPanSum = std::cos(angle) + std::sin(angle);
    double sourceDry = 0, sourceHalo = 0, predictionError = 0;
    std::array<float, 96> delay{}; // physical-output guard latency:2ms at48kHz
    size_t delayIndex = 0;
    Halo result;
    for (int start = 0; start < 24000; start += 64) {
        const int n = std::min(64, 24000 - start);
        for (size_t k = 0; k < engine.size(); ++k)
            engine[k].RenderBatch(mainL.data(), mainR.data(), left[k].data(), right[k].data(),
                                  nullptr, nullptr, piezo[k].data(), n);
        for (int i = 0; i < n; ++i) {
            scene->BeginFrame(2);
            for (int sub = 0; sub < 2; ++sub) {
                float close = 0, far = 0, pickup = 0;
                const bool active = voice.ProcessSample(close, far, pickup);
                mesh.BeginSample();
                if (active) mesh.AccumulateVoice(note, tine ? voice.GetBridgeForce() * .01 : voice.GetMechanicalSample());
                float haloL = 0, haloR = 0;
                mesh.Process(1, haloL, haloR);
                double commonReferenceDry = close * oldPanSum;
                if (tine) {
                    maremba::KalimbaBoxDrive drive;
                    drive.Add(active ? voice.GetBridgeForce() : 0, 1, 0, maremba::KalimbaTineSide(note));
                    double boxClose = 0, boxFar = 0, plate = 0, side = 0;
                    board.Process(drive, .6, .8, boxClose, boxFar, plate, side);
                    commonReferenceDry += .072 * boxClose; // former half-per-ear board sum
                }
                sourceDry += commonReferenceDry * commonReferenceDry;
                sourceHalo += .49 * (haloL + haloR) * (haloL + haloR);
                for (int key = 0; key < mesh.ReceiverCount(); ++key) {
                    const float receiver = mesh.GetReceiverPressure(key, 1);
                    // Deliberately use the generic API and independent legacy
                    // pair-sum calibration; this checks production's compact
                    // source API and actual per-receiver geometry together.
                    scene->Add(maremba::CaptureSourceIndex(static_cast<maremba::EMarimbaModel>(model), mesh.ReceiverNote(key)),
                               sub, .35f * receiver, .35f * receiver, .21f * receiver);
                }
            }
            float expectedL = 0, expectedR = 0, roomL = 0, roomR = 0;
            scene->Process(expectedL, expectedR, roomL, roomR);
            const double prediction = delay[delayIndex];
            delay[delayIndex] = (expectedL + expectedR) * (.65f * .24f);
            delayIndex = (delayIndex + 1) % delay.size();
            if ((start + i) % 16 == 15) { voice.UpdateEnvelope(); mesh.FlushTiny(); board.FlushTiny(); scene->FlushTiny(); }
            const double x = left[0][i] + right[0][i];
            const double delta = left[1][i] + right[1][i] - x;
            dry += x*x;
            wet += delta*delta;
            predictionError += (delta - prediction) * (delta - prediction);
            result.pickupIdentical &= piezo[0][i] == piezo[1][i];
            result.finite &= std::isfinite(x) && std::isfinite(delta);
        }
    }
    result.ratio = wet / std::max(dry, 1.e-30);
    result.sourceRatio = sourceHalo / std::max(sourceDry, 1.e-30);
    result.sceneResidual = std::sqrt(predictionError / std::max(wet, 1.e-30));
    return result;
}
struct Restrike { double worst = 0, absolute = 0; };
Restrike couplingRestrike(int model) {
    Restrike result;
    for (int note : {48, 60, 72}) for (int phase = 0; phase < 24; ++phase) {
        std::array<maremba::MarembaVoice, 2> voice;
        std::array<maremba::FastPRNG, 2> random{maremba::FastPRNG(7), maremba::FastPRNG(7)};
        auto trigger = [&](size_t k, float velocity, float coupling) {
            voice[k].Trigger(note, velocity, static_cast<maremba::EMarimbaModel>(model), .2f, .35f,
                             0, coupling, 1, 0, 0, 0, 96000, random[k], 0, 0, 0, 0);
        };
        trigger(0, 1, 1);
        trigger(1, 1, 1);
        // Sample different cavity phases. Both voices then receive the SAME
        // quiet stroke; only the newly latched Coupling setting differs.
        const int at = 1920 + phase * 17;
        double energy = 0;
        for (int i = 0; i < at; ++i) for (size_t k = 0; k < voice.size(); ++k) {
            float close, far, piezo;
            voice[k].ProcessSample(close, far, piezo);
            if (k == 0 && i >= at - 400) energy += close*close;
            if (i % 32 == 31) voice[k].UpdateEnvelope();
        }
        trigger(0, .01f, 1);
        trigger(1, .01f, 0);
        std::array<float, 2> close{};
        for (size_t k = 0; k < voice.size(); ++k) {
            float far, piezo;
            voice[k].ProcessSample(close[k], far, piezo);
        }
        const double difference = std::abs(close[1] - close[0]);
        const double ratio = difference / std::sqrt(std::max(energy / 400, 1.e-30));
        result.worst = std::max(result.worst, ratio);
        result.absolute = std::max(result.absolute, difference);
    }
    return result;
}
struct Roll { double peak = 0, early = 0, late = 0, idleSeconds = 2; bool finite = true, voicesDrained = false, drained = false; };
Roll cornerRoll(int model, int note) {
    auto p = clean(model);
    p.malletType = note == 48 ? 0 : 3;
    p.malletHardness = note == 48 ? 0 : 1;
    p.strikePosition = note == 48 ? .05f : .95f;
    p.decay = 8;
    p.resonatorCoupling = p.artifacts = p.buzzAmount = p.bodyBloom = p.sympathetic = 1;
    maremba::MarembaEngine engine(rate);
    engine.SetParameters(p);
    Roll result;
    std::array<float, 64> left{}, right{}, closeL{}, closeR{}, piezo{};
    auto render = [&](int frames, int window) {
        for (int start = 0; start < frames; start += 64) {
            const int n = std::min(64, frames - start);
            engine.RenderBatch(left.data(), right.data(), closeL.data(), closeR.data(),
                               nullptr, nullptr, piezo.data(), n);
            for (int i = 0; i < n; ++i) {
                const double close = closeL[i] + closeR[i];
                result.peak = std::max(result.peak, std::abs(close));
                result.finite &= std::isfinite(close) && std::isfinite(piezo[i]);
                if (window == 1) result.early += close*close;
                if (window == 2) result.late += close*close;
            }
        }
    };
    // Forty strokes/second with alternating force; the final stop occurs while
    // a soft contact or a pending artifact can still be in flight.
    for (int hit = 0; hit < 20; ++hit) {
        engine.NoteOn(note, hit % 2 ? .25f : 1.f);
        render(1200, hit < 8 ? 1 : 0);
    }
    engine.NoteOn(note, 1);
    render(24, 0);
    engine.DampAll();
    render(86400, 0);
    render(9600, 2);
    result.voicesDrained = engine.GetActiveVoiceCount() == 0;
    // The expanded low-frequency support bank can ring inaudibly beyond two
    // seconds. Engine idle deliberately waits below -180 dBFS plus an echo-gap
    // hold, so audit that resource boundary separately from audible stop.
    while (!engine.IsSilent() && result.idleSeconds < 4) {
        render(4800, 0);
        result.idleSeconds += .1;
    }
    result.drained = engine.IsSilent();
    return result;
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool success, const std::string& message) {
        if (!success) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    };
    std::cout << std::setprecision(8);
    for (int model = 0; model < 4; ++model) {
        maremba::SympatheticMesh mesh;
        mesh.Configure(96000, 0, static_cast<maremba::EMarimbaModel>(model));
        double selfEnergy = 0, haloEnergy = 0;
        for (int i = 0; i < 24000; ++i) {
            mesh.BeginSample();
            mesh.AccumulateVoice(60, static_cast<float>(.1 * std::sin(maremba::kTwoPiD * 261.625565 * i / 96000)));
            float left, right;
            mesh.Process(1, left, right);
            const double own = mesh.GetResonatorOutput(60 - firstKey<maremba::SympatheticMesh>(0));
            selfEnergy += own*own;
            haloEnergy += (left+right)*(left+right);
        }
        check(selfEnergy == 0, "sympathetic bank duplicates its driven key model=" + std::to_string(model));
        check(haloEnergy > 1.e-8, "removing self response also silences neighboring keys");
        std::cout << "NEIGHBORS model=" << model << " self_energy=" << selfEnergy
                  << " neighbor_output_energy=" << haloEnergy << '\n';

        constexpr std::array<int, 11> notes = {36, 47, 48, 49, 60, 70, 71, 72, 73, 84, 96};
        std::array<double, notes.size()> levels{};
        for (size_t n = 0; n < notes.size(); ++n) {
            const auto halo = haloResponse(model, notes[n]);
            levels[n] = db(halo.ratio);
            check(halo.finite && halo.pickupIdentical, "halo is unbounded or leaks into isolated pickup");
            check(halo.sceneResidual < 2.e-4,
                  "halo differs from independently calibrated receiver-scene prediction model=" + std::to_string(model)
                  + " note=" + std::to_string(notes[n]));
            // Permit natural register variation, but not the old effectively
            // dead control above/below the hard-coded two-octave bank.
            if (notes[n] == 36 || notes[n] == 84 || notes[n] == 96)
                check(levels[n] > -60 && db(halo.sourceRatio) > -60 && db(halo.sourceRatio) < 0,
                      "Sympathetic lacks useful keyboard coverage model=" + std::to_string(model)
                      + " note=" + std::to_string(notes[n]));
            std::cout << "REGISTER model=" << model << " note=" << notes[n]
                      << " halo_to_dry_db=" << levels[n] << " source_halo_to_dry_db=" << db(halo.sourceRatio)
                      << " scene_prediction_residual=" << halo.sceneResidual << '\n';
        }
        double worstStep = 0;
        for (size_t n : {size_t(2), size_t(3), size_t(6), size_t(7), size_t(8)})
            worstStep = std::max(worstStep, std::abs(levels[n] - levels[n-1]));
        // Real keys answer only partials that coincide with them, so
        // neighbouring notes can differ by several dB; a bank edge would
        // silence one side outright.
        check(worstStep < 10, "Sympathetic has an artificial former-bank-edge cliff model=" + std::to_string(model));
        std::cout << "REGISTER_EDGE model=" << model << " largest_semitone_step_db=" << worstStep << '\n';

        const auto restrike = couplingRestrike(model);
        check(restrike.worst < .01, "quiet restrike abruptly cuts the stored cavity readout model=" + std::to_string(model));
        std::cout << "COUPLING_RESTRIKE model=" << model << " first_sample_delta_to_ring_rms="
                  << restrike.worst << " largest_absolute_delta=" << restrike.absolute << '\n';
        for (int note : {48, 72}) {
            const auto roll = cornerRoll(model, note);
            const double tail = db(roll.late / std::max(roll.early, 1.e-30));
            check(roll.finite && roll.early > 1.e-8, "dense corner roll loses its audible finite note");
            check(roll.voicesDrained && tail < -70, "transport stop leaves a dense corner roll ringing");
            check(roll.drained, "stopped dense corner roll never releases its inaudible shared tail");
            std::cout << "CORNER_ROLL model=" << model << " note=" << note << " peak=" << roll.peak
                      << " late_to_early_db=" << tail << " voices_drained=" << roll.voicesDrained
                      << " idle_by_seconds=" << roll.idleSeconds << " drained=" << roll.drained << '\n';
        }
    }
    std::cout << "Round3ResponseTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
