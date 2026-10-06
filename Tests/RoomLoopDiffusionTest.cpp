// Short loop diffusion: passivity, delay budget, realtime lifecycle and legacy
// opt-out. Build with -DMAREMBA_LOOP_BASELINE against a frozen DSP directory
// and --dump-legacy FILE to compare the old and new disabled paths byte-for-byte.
#include "RoomCapture.h"
#include "RoomAlgorithms.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace { bool counting = false; std::size_t allocations = 0; }
void* operator new(std::size_t size) {
    if (counting) ++allocations;
    if (void* p = std::malloc(std::max(size, std::size_t{1}))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace maremba {
struct RoomCaptureTestAccess {
    static NetworkVoicing Voicing(int type) { return RoomCapture::Voicing(type); }
};
#ifndef MAREMBA_LOOP_BASELINE
struct FeedbackNetworkTestAccess {
    static const auto& Diffusers(const FeedbackNetwork& n) { return n.mLoopDiffuser; }
    static int Span(const FeedbackNetwork& n) { return n.mLoopDiffuserSpan; }
};
#endif
}

namespace {
using namespace maremba;
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
constexpr std::array<double, 6> kRates{{8000, 44100, 48000, 88200, 96000, 192000}};
float stimulus(int frame) {
    if (frame == 0) return .25f;
    if (frame >= 4096) return 0;
    const unsigned value = static_cast<unsigned>(frame) * 1664525u + 1013904223u;
    return (static_cast<float>((value >> 8) & 65535u) / 65535.f - .5f) * .1f;
}

// Compile this same stimulus against the frozen original headers. No expected
// hash is platform-specific, and the comparison observes every output sample.
void dumpLegacy(const char* path) {
    std::ofstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "cannot create legacy comparison output");
    for (double rate : kRates) for (int type = 0; type < 3; ++type) {
        // Fixed input design data, independent of later production room voicing.
        // Existing aggregates must continue to default to legacy loop behavior.
        NetworkVoicing voicing{{.0043,.0053,.0061,.0071,.0079,.0089,
                                .0101,.0113,.0127,.0139,.0153,.0171},
                               {.00131,.00217,.00307}, .61, {1.,1.,.7,.4}};
        for (auto& seconds : voicing.lines) seconds *= 1 + .5 * type;
        for (bool fractional : {false, true}) for (unsigned seed : {0u, 0x73231u}) {
            FeedbackNetwork network;
            network.memory.assign(network.Layout(voicing, 192000), 0);
            network.SetSampleRate(voicing, rate, fractional, seed);
            for (int i = 0; i < static_cast<int>(rate * .2); ++i) {
                float frame[2]; network.Process(stimulus(i), frame[0], frame[1]);
                file.write(reinterpret_cast<const char*>(frame), sizeof(frame));
            }
        }
        RoomBank bank;
        bank.Allocate(192000); bank.SetSampleRate(rate); bank.SetType(type);
        for (int i = 0; i < static_cast<int>(rate * .2); ++i) {
            float frame[4];
            bank.Process(stimulus(i), .7f * stimulus(i), .25f,
                         frame[0], frame[1], frame[2], frame[3]);
            file.write(reinterpret_cast<const char*>(frame), sizeof(frame));
        }
    }
    require(static_cast<bool>(file), "legacy comparison output write failed");
}

#ifndef MAREMBA_LOOP_BASELINE
void allpassContracts() {
    constexpr double pi = 3.14159265358979323846;
    double worstEnergyError = 0, worstMagnitudeError = 0;
    for (double delay : {0., .75, 1., 1.1, 1.5, 1.999, 2., 2.2, 7.75, 29.3, 127.7, 512.2})
    for (float coefficient : {-.35f, 0.f, .31f, .35f}) {
        RoomLoopDiffuser diffuser;
        const double exact = delay >= 1 ? delay : 0;
        const int cells = std::max(0, static_cast<int>(std::floor(exact)) - 1);
        std::vector<float> storage(static_cast<std::size_t>(cells + 2), 123.f);
        float* const state = storage.data() + 1;
        const int frames = 80 * static_cast<int>(std::ceil(std::max(delay, 1.))) + 1;
        std::vector<float> impulse(static_cast<std::size_t>(frames));
        counting = true;
        diffuser.Configure(delay, coefficient); diffuser.Reset(state);
        for (int i = 0; i < frames; ++i) impulse[static_cast<std::size_t>(i)] = diffuser.Process(i == 0 ? 1.f : 0.f, state);
        counting = false;
        require(allocations == 0, "allpass configuration/reset/process allocated");
        require(diffuser.Delay() == cells && diffuser.ExactDelay() == exact, "configured allpass delay differs from its causal core");
        require(storage.front() == 123.f && storage.back() == 123.f, "allpass writes outside its delay storage");
        double energy = 0;
        for (float sample : impulse) energy += static_cast<double>(sample) * sample;
        worstEnergyError = std::max(worstEnergyError, std::abs(energy - 1));
        require(std::abs(energy - 1) < 2.e-6, "allpass impulse loses or creates energy");
        for (double angle : {0., .013*pi, .11*pi, .59*pi, .97*pi, pi}) {
            std::complex<double> response{};
            for (int i = 0; i < frames; ++i)
                response += static_cast<double>(impulse[static_cast<std::size_t>(i)]) * std::polar(1., -angle*i);
            worstMagnitudeError = std::max(worstMagnitudeError, std::abs(std::abs(response) - 1));
            require(std::abs(std::abs(response) - 1) < 2.e-6, "allpass magnitude differs from unity");
            // Independent transfer function catches an added/lost sample and
            // the algebraic one-sample core, not just a flat magnitude.
            std::complex<double> expected{1., 0.};
            if (exact > 0) {
                const double fractional = exact - cells;
                const double a = (1 - fractional) / (1 + fractional);
                const auto z = std::polar(1., -angle);
                const auto core = std::polar(1., -angle*cells) * (a + z) / (1. + a*z);
                expected = (core - static_cast<double>(coefficient)) / (1. - static_cast<double>(coefficient)*core);
            }
            require(std::abs(response - expected) < 2.e-6, "allpass phase disagrees with its exact transfer function");
        }
        diffuser.Reset(state);
        for (int i = 0; i < frames; ++i)
            require(diffuser.Process(i == 0 ? 1.f : 0.f, state) == impulse[static_cast<std::size_t>(i)],
                    "allpass reset does not reproduce its impulse");
        diffuser.Reset(state);
        double inputEnergy = 0, outputEnergy = 0;
        for (int i = 0; i < frames; ++i) {
            const float input = stimulus(i), output = diffuser.Process(input, state);
            inputEnergy += static_cast<double>(input)*input;
            outputEnergy += static_cast<double>(output)*output;
            require(outputEnergy <= inputEnergy + 2.e-6 * std::max(inputEnergy, 1.),
                    "allpass creates energy before its stored tail is released");
            if (exact == 0) require(output == input, "sub-sample allpass is not exact bypass");
        }
        const double g = std::abs(static_cast<double>(coefficient));
        const double expected = exact * (1 + g) / (1 - g);
        require(std::abs(diffuser.MaximumGroupDelay() - expected) < 1.e-12,
                "reported maximum group delay disagrees with analytic bound");
        diffuser.Reset(state);
        for (int i = 0; i < frames; ++i) require(diffuser.Process(0, state) == 0, "reset allpass leaks a tail");
    }
    std::cout << "  impulse energy error " << worstEnergyError << ", magnitude error " << worstMagnitudeError << '\n';
}

void networkContracts() {
    double worstDelayRatio = 0;
    for (int type = 0; type < 3; ++type) {
        auto voicing = RoomCaptureTestAccess::Voicing(type);
        require(voicing.loopDiffusion, "production room does not opt into loop diffusion");
        FeedbackNetwork network;
        network.memory.assign(network.Layout(voicing, 192000), 0);
        const auto* address = network.memory.data();
        const auto capacity = network.memory.capacity();
        for (double rate : kRates) {
            counting = true;
            network.SetSampleRate(voicing, rate, true, 0x74352u);
            counting = false;
            require(allocations == 0, "network rate configuration allocated");
            for (std::size_t line = 0; line < voicing.lines.size(); ++line) {
                const auto& diffuser = FeedbackNetworkTestAccess::Diffusers(network)[line];
                const double ratio = diffuser.MaximumGroupDelay() / (voicing.lines[line] * rate);
                worstDelayRatio = std::max(worstDelayRatio, ratio);
                require(ratio <= .0395, "loop diffuser exceeds its 3.95% added delay budget");
                require(diffuser.Delay() < FeedbackNetworkTestAccess::Span(network), "loop diffuser exceeds allocated span");
            }
            std::array<std::array<float, 2>, 4096> reference{};
            counting = true;
            for (std::size_t i = 0; i < reference.size(); ++i)
                network.Process(stimulus(static_cast<int>(i)), reference[i][0], reference[i][1]);
            network.FlushStates(); network.Reset();
            for (std::size_t i = 0; i < reference.size(); ++i) {
                float left, right; network.Process(stimulus(static_cast<int>(i)), left, right);
                require(std::isfinite(left) && std::isfinite(right), "diffused network is nonfinite");
                require(left == reference[i][0] && right == reference[i][1], "network reset does not replay exactly");
            }
            network.Reset();
            for (int i = 0; i < 4096; ++i) {
                float left, right; network.Process(0, left, right);
                require(left == 0 && right == 0, "network reset leaves audible state");
            }
            counting = false;
            require(allocations == 0, "network processing/reset allocated");
            require(address == network.memory.data() && capacity == network.memory.capacity(), "network storage moved during realtime lifecycle");
        }
    }
    std::cout << "  maximum added loop group delay " << 100 * worstDelayRatio << "%\n";
}

#endif
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--dump-legacy") { dumpLegacy(argv[2]); return 0; }
#ifndef MAREMBA_LOOP_BASELINE
    allpassContracts(); networkContracts();
    std::cout << "RoomLoopDiffusionTest PASSED checks=" << checks << '\n';
    return 0;
#else
    std::cerr << "Frozen baseline requires --dump-legacy FILE\n";
    return 2;
#endif
}
