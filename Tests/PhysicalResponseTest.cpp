// Perceptual response contracts through the public engine audio outputs.
// Complements TouchResponseTest (contact/velocity/position) and the mechanical
// artifact tests. No private states, modal coefficients, or exact waveforms.
// Build with -I Source/DSP, this file and Source/DSP/*.cpp. --measure reports without gating.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr double rate = 48000;
constexpr double pi = 3.14159265358979323846;
struct Audio {
    std::vector<double> close, piezo, mainLeft;
    double mainPeak = 0;
};
using Field = float maremba::EngineParameters::*;
maremba::EngineParameters controlled(int model) {
    maremba::EngineParameters p;
    p.model = model;
    p.strikePosition = .35f;
    p.artifacts = p.strikeJitter = p.detune = 0;
    p.sympathetic = p.bodyBloom = p.buzzAmount = 0;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
    p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
    p.closeLevel = p.volume = 1;
    return p;
}
Audio render(const maremba::EngineParameters& p, int note = 60, float velocity = .85f,
             double seconds = 1, Field automation = nullptr, float target = 0) {
    maremba::MarembaEngine engine(rate);
    engine.SetParameters(p);
    engine.NoteOn(note, velocity);
    Audio a;
    const size_t frames = static_cast<size_t>(rate * seconds);
    a.close.resize(frames);
    a.piezo.resize(frames);
    a.mainLeft.resize(frames);
    std::array<float, 64> l{}, r{}, cl{}, cr{}, pickup{};
    for (size_t start = 0; start < frames; start += l.size()) {
        if (automation && start == 4800) {
            auto changed = p;
            changed.*automation = target;
            engine.SetParameters(changed);
        }
        const int n = static_cast<int>(std::min(l.size(), frames - start));
        engine.RenderBatch(l.data(), r.data(), cl.data(), cr.data(), nullptr, nullptr,
                           pickup.data(), n);
        for (int i = 0; i < n; ++i) {
            a.close[start + i] = cl[i] + cr[i];
            a.piezo[start + i] = pickup[i];
            a.mainLeft[start + i] = l[i];
            a.mainPeak = std::max({a.mainPeak, std::abs(static_cast<double>(l[i])),
                                  std::abs(static_cast<double>(r[i]))});
        }
    }
    return a;
}
double db(double x) { return 10 * std::log10(std::max(x, 1e-30)); }
double energy(const std::vector<double>& x, double start = 0, double stop = 1) {
    double e = 0;
    const size_t first = static_cast<size_t>(start * rate);
    const size_t last = std::min(x.size(), static_cast<size_t>(stop * rate));
    for (size_t i = first; i < last; ++i) e += x[i] * x[i];
    return e / std::max(size_t(1), last - first);
}
std::vector<double> difference(const Audio& a, const Audio& b) {
    std::vector<double> d(a.close.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = a.close[i] - b.close[i];
    return d;
}
double residual(const std::vector<double>& a, const std::vector<double>& b,
                double start, double stop) {
    double aa = 0, bb = 0, ab = 0;
    for (size_t i = static_cast<size_t>(start * rate); i < static_cast<size_t>(stop * rate); ++i) {
        aa += a[i] * a[i];
        bb += b[i] * b[i];
        ab += a[i] * b[i];
    }
    return std::max(0., 1 - ab * ab / std::max(aa * bb, 1e-30));
}
double bandFraction(const std::vector<double>& x, double hz, bool high) {
    // Second-order Butterworth gives a broad band, independent of FFT bins and
    // exact modal tuning. Express its energy relative to the unfiltered signal.
    const double k = std::tan(pi * hz / rate), norm = 1 / (1 + std::sqrt(2.) * k + k*k);
    const double b0 = high ? norm : k*k*norm;
    const double b1 = (high ? -2 : 2) * b0;
    const double a1 = 2*(k*k-1)*norm, a2 = (1-std::sqrt(2.)*k+k*k)*norm;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0, total = 0;
    for (double v : x) {
        const double y = b0*v+b1*x1+b0*x2-a1*y1-a2*y2;
        x2=x1; x1=v; y2=y1; y1=y;
        e += y*y;
        total += v*v;
    }
    return e/std::max(total, 1e-30);
}
bool finite(const Audio& a) {
    for (const auto* channel : {&a.close, &a.piezo})
        for (double x : *channel) if (!std::isfinite(x) || std::abs(x) > 4) return false;
    return true;
}
double renderMainPeak(maremba::MarembaEngine& engine, int frames) {
    std::array<float, 64> left{}, right{};
    double peak = 0;
    for (int start = 0; start < frames; start += static_cast<int>(left.size())) {
        const int n = std::min(static_cast<int>(left.size()), frames - start);
        engine.RenderBatch(left.data(), right.data(), nullptr, nullptr, nullptr, nullptr, nullptr, n);
        for (int i = 0; i < n; ++i)
            peak = std::max({peak, std::abs(static_cast<double>(left[i])),
                             std::abs(static_cast<double>(right[i]))});
    }
    return peak;
}
} // namespace

int main(int argc, char** argv) {
    const bool measure = argc == 2 && std::string(argv[1]) == "--measure";
    if (argc > 1 && !measure) return 2;
    int failures = 0;
    auto check = [&](bool pass, const std::string& message) {
        if (pass) return;
        if (!measure) ++failures;
        std::cerr << (measure ? "OBSERVATION " : "FAIL ") << message << '\n';
    };
    std::cout << std::setprecision(8);
    for (int model = 0; model < 4; ++model) {
        for (int note : {48, 72}) {
            auto p = controlled(model);
            double previous = -300;
            // Kalimba now reserves its full upper travel for only 2x natural
            // sustain. Compare that endpoint with natural/short settings.
            const float longDecay = model == 3 ? 8.f : 2.5f;
            for (float decay : {.4f, 1.f, longDecay}) {
                p.decay = decay;
                const auto a = render(p, note);
                // Decay stretches the whole ring, including the retained
                // Padauk/Balafon cavity bloom. Read bass tails after that
                // buildup rather than mistaking a later attack for sustain.
                const double from = note < 60 ? .60 : .30;
                const double tail = db(energy(a.close, from, from + .3) / energy(a.close, .02, .08));
                check(finite(a), "unbounded decay output");
                check(tail > previous + 2, "Decay fails to lengthen normalized tail model="
                      + std::to_string(model) + " note=" + std::to_string(note));
                previous = tail;
                std::cout << "DECAY model=" << model << " note=" << note << " decay=" << decay
                          << " tail_to_early_db=" << tail << '\n';
            }

            p.decay = 1;
            p.resonatorCoupling = 0;
            p.resonatorTune = -50;
            const auto isolatedLow = render(p, note);
            p.resonatorTune = 50;
            const auto isolatedHigh = render(p, note);
            check(isolatedLow.close == isolatedHigh.close && isolatedLow.piezo == isolatedHigh.piezo,
                  "disconnected resonator tuning changes the bar");
        }

        // Tune a key near the air resonance. Bars have per-key resonators;
        // Kalimba has one fixed 400 Hz box, nearest to G4 (MIDI 67, 392 Hz).
        // Testing C4 (262 Hz) would instead measure its weak off-resonance
        // response, strongly dependent on the contact sensor's spatial weighting.
        const int tuneNote = model == 3 ? 67 : 60;
        auto tune = controlled(model);
        tune.resonatorCoupling = 0;
        tune.resonatorTune = -50;
        const auto disconnectedLow = render(tune, tuneNote);
        tune.resonatorTune = 50;
        const auto disconnectedHigh = render(tune, tuneNote);
        check(disconnectedLow.close == disconnectedHigh.close
              && disconnectedLow.piezo == disconnectedHigh.piezo,
              "disconnected resonator tuning changes the resonance-test key model="
              + std::to_string(model));
        tune.resonatorCoupling = 1;
        tune.resonatorTune = -50;
        const auto low = render(tune, tuneNote);
        tune.resonatorTune = 50;
        const auto high = render(tune, tuneNote);
        const double tuning = residual(low.close, high.close, .008, .30);
        const double reaction = residual(low.piezo, high.piezo, .008, .50);
        check(tuning > .0001, "coupled resonator tune lacks audible timbral response model="
              + std::to_string(model));
        // The bars' contact readout senses cavity feedback into bar motion.
        // Kalimba instead has body-dependent tine damping and a shared plate
        // pickup: its changed contact output includes both loading and plate
        // motion, and does not establish dynamic feedback into a tine cavity.
        check(reaction > (model == 3 ? .000005 : .00001), "resonator loading lacks a contact-pickup response model="
              + std::to_string(model));
        std::cout << "TUNE model=" << model << " note=" << tuneNote << " gain_fit_residual=" << tuning
                  << " pickup_loading_residual=" << reaction << '\n';

        // Isolate the added acoustic layers from the dry bar at the direct mic,
        // with no room, limiter, preamp, random errors, or velocity modulation.
        // Endpoint contribution must be audible, restrained and free of rumble.
        auto p = controlled(model);
        const auto dry = render(p);
        const std::array<Field, 3> fields = {&maremba::EngineParameters::bodyBloom,
            &maremba::EngineParameters::sympathetic, &maremba::EngineParameters::buzzAmount};
        const std::array<const char*, 3> names = {"body", "sympathetic", "buzz"};
        for (size_t f = 0; f < fields.size(); ++f) {
            // Kalimba buzzers lift only when the board swings hard: a forte
            // pick on a treble tine (a flesh or natural-thumb C4 leaves them
            // still, as on the instrument).
            const bool buzzers = model == 3 && fields[f] == &maremba::EngineParameters::buzzAmount;
            auto base = p;
            if (buzzers) base.malletType = 3;
            const int note = buzzers ? 84 : 60;
            const float velocity = buzzers ? 1.f : .85f;
            const auto reference = buzzers ? render(base, note, velocity) : dry;
            auto q = base;
            q.*fields[f] = .35f;
            const auto mild = render(q, note, velocity);
            q.*fields[f] = 1;
            const auto full = render(q, note, velocity);
            const auto smallDelta = difference(mild, reference), delta = difference(full, reference);
            const double contribution = db(energy(delta) / energy(reference.close));
            const double growth = db(energy(delta) / std::max(energy(smallDelta), 1e-30));
            const double rumble = db(bandFraction(delta, 25, false));
            const double late = db(energy(delta, .7, 1) / std::max(energy(delta, .04, .3), 1e-30));
            check(finite(full), "unbounded character output");
            // Body and halo are purely acoustic returns. A resonator membrane
            // is acoustic too, but it drains the air that loads the bar, so
            // the bar-mounted pickup may change slightly and only indirectly.
            // Kalimba buzzers sit on the board itself and may knock the pickup.
            if (fields[f] != &maremba::EngineParameters::buzzAmount) {
                check(full.piezo == reference.piezo,
                      std::string(names[f]) + " acoustic layer leaks into isolated contact pickup");
            } else if (model != 3) {
                // The drain shortens the bar's ring (low frequencies), but the
                // buzz itself (its upper band) must stay out of the pickup.
                const double k = std::tan(pi * 2000 / rate), n = 1 / (1 + std::sqrt(2.) * k + k * k);
                const double b0 = n, b1 = -2 * n, b2 = n, a1 = 2 * (k * k - 1) * n, a2 = (1 - std::sqrt(2.) * k + k * k) * n;
                std::vector<double> pickupDelta(full.piezo.size());
                double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                for (size_t i = 0; i < pickupDelta.size(); ++i) {
                    const double d = full.piezo[i] - reference.piezo[i];
                    const double y = b0 * d + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                    x2 = x1; x1 = d; y2 = y1; y1 = y;
                    pickupDelta[i] = y;
                }
                check(db(energy(pickupDelta) / std::max(energy(reference.piezo), 1e-30)) < -40,
                      "buzz acoustic layer leaks into isolated contact pickup");
            }
            if (model == 0 && (fields[f] == &maremba::EngineParameters::bodyBloom
                           || fields[f] == &maremba::EngineParameters::sympathetic)) {
                // Accepted Rosewood retains a quiet frame/neighbor response
                // under the bar even at the full control setting.
                check(contribution > -38 && contribution < -28,
                      "Rosewood shared radiation masks the bar or loses its response");
            }
            if (model == 1 && fields[f] == &maremba::EngineParameters::bodyBloom) {
                // Padauk's accepted balance keeps a useful full-Bloom return
                // below the bar/box voice, including its broad bass emphasis.
                check(contribution > -26 && contribution < -18,
                      "Padauk frame masks the voice or loses useful Bloom response");
            }
            if (model == 2 && fields[f] == &maremba::EngineParameters::bodyBloom) {
                // The accepted Balafon midpoint keeps the full-Bloom frame
                // audible beneath the key/gourd without its former dominance.
                check(contribution > -11 && contribution < -4,
                      "Balafon frame masks the voice or loses useful Bloom response");
            }
            check(contribution > -45 && contribution < 6,
                  std::string(names[f]) + " contribution inaudible or dominates the bar model="
                  + std::to_string(model));
            check(growth > 3, std::string(names[f]) + " lacks directional amount response");
            check(rumble < -15, std::string(names[f]) + " accumulates sub-audible rumble");
            check(late < -3, std::string(names[f]) + " retains an excessive late tail");
            std::cout << "CHARACTER model=" << model << " layer=" << names[f]
                      << " delta_to_dry_db=" << contribution << " amount_growth_db=" << growth
                      << " sub25_fraction_db=" << rumble << " late_to_early_db=" << late << '\n';
        }

        // A held force/displacement cannot produce a sustained acoustic tone.
        // Test the shared blocks directly so the bar's own decay cannot conceal
        // DC feedthrough. The early step response also proves they are active.
        maremba::FrameBody body;
        maremba::SympatheticMesh mesh;
        const auto params = maremba::GetModelParams(static_cast<maremba::EMarimbaModel>(model));
        body.Configure(rate, params.frameBodyFreq, params.frameBodyQ,
                       static_cast<maremba::EMarimbaModel>(model));
        mesh.Configure(rate, 0, static_cast<maremba::EMarimbaModel>(model));
        std::array<double, 2> early{}, late{}, lateMean{};
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            const double b = body.Process(.1f);
            mesh.BeginSample();
            mesh.AccumulateVoice(60, .1f);
            float l = 0, r = 0;
            mesh.Process(1, l, r);
            const std::array<double, 2> sample = {b, l + r};
            for (size_t s = 0; s < sample.size(); ++s) {
                if (i < .2 * rate) early[s] += sample[s] * sample[s];
                if (i >= .8 * rate) {
                    late[s] += sample[s] * sample[s];
                    lateMean[s] += sample[s] / (.2 * rate);
                }
            }
        }
        for (size_t s = 0; s < early.size(); ++s) {
            const double dc = db(late[s] / std::max(early[s], 1e-30));
            // The frame body is broad and settles fully. The sympathetic keys
            // are real keys (Q = 1/eta), so the step's edge leaves each ringing
            // with its own decay; what must not happen is a held output.
            if (s == 0)
                check(early[s] > 1e-8 && dc < -70, "body radiates a static held input");
            else
                check(early[s] > 1e-8 && dc < -10 && std::abs(lateMean[s]) < .05 * std::sqrt(late[s] / (.2 * rate)),
                      "sympathetic radiates a static held input");
            std::cout << "DC model=" << model << " layer=" << (s ? "sympathetic" : "body")
                      << " settled_to_step_db=" << dc << '\n';
        }
    }

    // Live gain changes should traverse their audible range over milliseconds,
    // rather than inserting a sample step. Compare with low/high steady renders
    // at the SAME time, avoiding zero crossings and the note's natural decay.
    const std::array<Field, 7> liveFields = {&maremba::EngineParameters::bodyBloom,
        &maremba::EngineParameters::sympathetic, &maremba::EngineParameters::closeLevel,
        &maremba::EngineParameters::farLevel, &maremba::EngineParameters::piezoLevel,
        &maremba::EngineParameters::stereoWidth, &maremba::EngineParameters::preampDrive};
    const std::array<const char*, 7> liveNames = {"body", "sympathetic", "close", "room",
                                               "piezo", "width", "drive"};
    for (size_t k = 0; k < liveFields.size(); ++k) {
        auto p = controlled(0);
        // Isolate control smoothing from output gain history. Steady high
        // and live-automated captures must both stay below headroom control.
        p.volume = .25f;
        p.closeLevel = p.piezoLevel = .4f;
        // Body/mesh automation also changes what enters the room. Disable that
        // history-dependent branch here; Room itself is probed with a fixed input.
        p.farLevel = 0;
        p.bodyBloom = p.sympathetic = .2f;
        p.*liveFields[k] = .2f;
        const auto low = render(p, 48, 1, .2);
        const auto changed = render(p, 48, 1, .2, liveFields[k], 1);
        p.*liveFields[k] = 1;
        const auto high = render(p, 48, 1, .2);
        for (const Audio* audio : {&low, &changed, &high})
            check(audio->mainPeak > 1e-6 && audio->mainPeak < .5,
                  std::string(liveNames[k]) + " automation fixture is inaudible or invokes headroom control");
        const size_t latency = maremba::OutputHeadroom::LatencyFrames(rate);
        double edge = 0, endpoint = 0, settled = 0, settledRange = 0;
        for (size_t i = 4800 + latency; i < changed.mainLeft.size(); ++i) {
            const double delta = changed.mainLeft[i] - low.mainLeft[i];
            const double range = high.mainLeft[i] - low.mainLeft[i];
            if (i < 4896 + latency) { edge += delta*delta; endpoint += range*range; }
            if (i >= 7200 + latency) {
                const double error = changed.mainLeft[i] - high.mainLeft[i];
                settled += error*error;
                settledRange += range*range;
            }
        }
        const double onset = db(edge/std::max(endpoint, 1e-30));
        const double error = db(settled/std::max(settledRange, 1e-30));
        check(endpoint > 1e-10 && onset < -8,
              std::string(liveNames[k]) + " automation inserts a gain step");
        check(error < -35, std::string(liveNames[k]) + " automation does not reach its target");
        std::cout << "AUTOMATION control=" << liveNames[k] << " onset_to_full_step_db=" << onset
                  << " settled_error_db=" << error << " endpoint_energy=" << endpoint
                  << " fixture_peak=" << std::max({low.mainPeak, changed.mainPeak, high.mainPeak}) << '\n';
    }

    // Idle rendering is elided, so all live-control ramps must finish at the
    // idle boundary. Otherwise a later note briefly uses an old audible gain,
    // even though the user has left every microphone at zero for a long time.
    {
        auto p = controlled(0);
        maremba::MarembaEngine engine(rate);
        engine.SetParameters(p);
        engine.NoteOn(60, .85f);
        renderMainPeak(engine, 4800);
        p.closeLevel = 0;
        p.oversampling = 1;
        engine.SetParameters(p);
        renderMainPeak(engine, 48000);
        check(engine.IsSilent(), "quality transition fails to enter idle");
        engine.NoteOn(60, .85f);
        const double peak = renderMainPeak(engine, 4800);
        check(peak == 0, "quality transition freezes a stale mic gain during idle");
        std::cout << "IDLE transition=quality muted_new_note_peak=" << peak << '\n';
    }
    {
        // Discover the natural idle boundary from the public engine state,
        // rather than depending on a particular model decay or silence timer.
        auto p = controlled(0);
        p.decay = .1f;
        maremba::MarembaEngine reference(rate);
        reference.SetParameters(p);
        reference.NoteOn(96, .85f);
        // An inaudible Room coefficient does not erase the recording's tail.
        // Allow three measured 60 dB decay intervals to reach the engine's
        // -180 dB capture threshold, plus source decay and the quiet hold.
        const auto roomDecay = maremba::RoomCapture::Decay(p.roomType, rate);
        const int drainBudget = static_cast<int>(rate * (.75 + 3 * std::max({
            roomDecay.t125, roomDecay.t500, roomDecay.t4k, roomDecay.t10k})));
        int idleFrame = 0;
        while (!reference.IsSilent() && idleFrame < drainBudget) {
            renderMainPeak(reference, 1);
            ++idleFrame;
            if (idleFrame == static_cast<int>(rate))
                check(reference.GetActiveVoiceCount() == 0, "short strike's mechanics failed to drain within one second");
        }
        check(reference.IsSilent() && idleFrame > 1, "short probe never reaches natural idle");
        if (reference.IsSilent() && idleFrame > 1) {
            maremba::MarembaEngine engine(rate);
            engine.SetParameters(p);
            engine.NoteOn(96, .85f);
            renderMainPeak(engine, idleFrame - 1);
            check(!engine.IsSilent(), "natural-idle control probe missed the active boundary");
            p.closeLevel = 0;
            engine.SetParameters(p);
            renderMainPeak(engine, 48000);
            check(engine.IsSilent(), "natural decay fails to enter idle");
            engine.NoteOn(60, .85f);
            const double peak = renderMainPeak(engine, 4800);
            check(peak == 0, "natural idle freezes a stale mic gain");
            std::cout << "IDLE transition=natural muted_new_note_peak=" << peak << '\n';
        }
    }
    std::cout << "PhysicalResponseTest " << (measure ? "MEASURED" : failures ? "FAILED" : "PASSED")
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
