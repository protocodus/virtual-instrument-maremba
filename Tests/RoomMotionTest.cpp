// Integration contract for slowly evolving late-room scattering. The static
// comparator has the same paths, microphones, absorption and FDN delays; only
// its scattering motion is disabled. No limiter or instrument excitation can
// conceal changes in the capture response.
#include "RoomCapture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace maremba {
struct RoomCaptureTestAccess {
    static void Static(RoomCapture& capture) {
        for (size_t type = 0; type < capture.mRoom.size(); ++type) {
            auto& room = capture.mRoom[type];
            for (size_t branch = 0; branch < room.tail.size(); ++branch) {
                auto field = RoomCapture::Voicing(static_cast<int>(type), branch);
                field.decay = room.voicing.decay;
                room.tail[branch].SetSampleRate(field, capture.mRate, true);
            }
        }
        capture.Reset();
    }
    static void EarlyOnly(RoomCapture& capture) {
        for (auto& room : capture.mRoom) room.tailGain = 0.0f;
    }
};
}

namespace {
using namespace maremba;
using Stereo = std::array<std::vector<float>, 2>;
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
std::unique_ptr<RoomCapture> makeRoom(int type, double rate) {
    auto room = std::make_unique<RoomCapture>();
    room->Allocate(rate, 1); room->SetSampleRate(rate); room->SetType(type);
    require(room->AddSource(0, {.2, -.15, .04}), "room-motion source setup");
    return room;
}
void silence(RoomCapture& room, int frames) {
    const float zero = 0;
    for (int i = 0; i < frames; ++i) {
        float left, right; room.Process(&zero, 1, left, right);
        require(left == 0 && right == 0, "silent room motion creates audio");
    }
}
Stereo impulse(RoomCapture& room, double seconds) {
    Stereo result;
    const auto frames = static_cast<size_t>(std::ceil(seconds * room.SampleRate()));
    for (auto& channel : result) channel.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const float input = i == 0 ? 1.0f : 0.0f;
        room.Process(&input, 1, result[0][i], result[1][i]);
        require(std::isfinite(result[0][i]) && std::isfinite(result[1][i]),
                "nonfinite moving room impulse response");
    }
    return result;
}
double energy(const Stereo& signal, size_t first = 0) {
    double result = 0;
    for (const auto& channel : signal)
        for (size_t i = first; i < channel.size(); ++i)
            result += static_cast<double>(channel[i]) * channel[i];
    return result;
}
double difference(const Stereo& a, const Stereo& b, size_t first = 0) {
    double result = 0;
    for (size_t channel = 0; channel < a.size(); ++channel)
        for (size_t i = first; i < a[channel].size(); ++i) {
            const double d = static_cast<double>(a[channel][i]) - b[channel][i];
            result += d * d;
        }
    return result;
}
// Sample differences emphasize fast changes. Report this residual metric;
// waveform roughness itself is not a discontinuity oracle for an impulse IR.
double differenceDerivative(const Stereo& a, const Stereo& b, size_t first) {
    double result = 0;
    for (size_t c = 0; c < a.size(); ++c)
        for (size_t i = std::max(first, size_t{1}); i < a[c].size(); ++i) {
            const double d = (static_cast<double>(a[c][i]) - b[c][i])
                           - (static_cast<double>(a[c][i - 1]) - b[c][i - 1]);
            result += d * d;
        }
    return result;
}
struct BandPass {
    double b0, b2, a1, a2, s1 = 0, s2 = 0;
    BandPass(double rate, double hz) {
        const double w = kTwoPiD * hz / rate, alpha = std::sin(w) / (2 * 4.32), a0 = 1 + alpha;
        b0 = alpha / a0; b2 = -b0; a1 = -2 * std::cos(w) / a0; a2 = (1 - alpha) / a0;
    }
    double process(double x) {
        const double y = b0 * x + s1; s1 = -a1 * y + s2; s2 = b2 * x - a2 * y; return y;
    }
};
double decay500(const Stereo& signal, double rate) {
    std::vector<double> remaining(signal[0].size(), 0);
    for (const auto& channel : signal) {
        BandPass a(rate, 500), b(rate, 500);
        for (size_t i = 0; i < channel.size(); ++i) {
            const double y = b.process(a.process(channel[i])); remaining[i] += y * y;
        }
    }
    for (size_t i = remaining.size() - 1; i > 0; --i) remaining[i - 1] += remaining[i];
    const auto start = static_cast<size_t>(.1 * rate);
    double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
    for (size_t i = start; i < remaining.size(); ++i) {
        const double y = 10 * std::log10(std::max(remaining[i] / remaining[start], 1.e-30));
        if (y > -5) continue;
        if (y < -30) break;
        const double x = i / rate; sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
    }
    require(n > 10, "room-motion decay regression has no usable tail");
    return -60 / ((n * sxy - sx * sy) / (n * sxx - sx * sx));
}

void responseAndClock() {
    for (double rate : {44100.0, 96000.0}) for (int type = 0; type < 3; ++type) {
        const auto prototype = makeRoom(type, rate);
        RoomCapture moving = *prototype, fixed = *prototype;
        RoomCaptureTestAccess::Static(fixed);
        const auto first = impulse(moving, 4.0), baseline = impulse(fixed, 4.0);
        moving.Reset();
        require(impulse(moving, 4.0) == first, "explicit reset fails exact moving-room replay");

        // All six walks have crossed at least one endpoint after this interval.
        // The input is silent, so only the room's elapsed-time state advances.
        RoomCapture shifted = *prototype, fixedShifted = *prototype;
        RoomCaptureTestAccess::Static(fixedShifted);
        const int elapsed = static_cast<int>(7.31 * rate);
        silence(shifted, elapsed); silence(fixedShifted, elapsed);
        RoomCapture neighboring = shifted;
        silence(neighboring, 1);
        const auto later = impulse(shifted, 4.0), adjacent = impulse(neighboring, 4.0);
        require(impulse(fixedShifted, 4.0) == baseline,
                "static room comparator is not time invariant");

        // Observe the actual direct/images/capsules independently of the late
        // output. Some early wall arrivals overlap the first diffuse arrivals,
        // so an arbitrary short time window would not isolate the geometry.
        RoomCapture early = *prototype, earlyLater = *prototype, earlyStatic = *prototype;
        RoomCaptureTestAccess::EarlyOnly(early);
        RoomCaptureTestAccess::EarlyOnly(earlyLater);
        RoomCaptureTestAccess::Static(earlyStatic);
        RoomCaptureTestAccess::EarlyOnly(earlyStatic);
        silence(earlyLater, elapsed);
        const auto directImages = impulse(early, .2);
        require(directImages == impulse(earlyLater, .2)
                && directImages == impulse(earlyStatic, .2),
                "room motion changes direct/early geometry or microphone response");

        const auto tail = static_cast<size_t>(.08 * rate);
        const double referenceTail = energy(baseline, tail), t500 = decay500(baseline, rate);
        require(referenceTail > 1.e-8 && t500 > 0, "vacuous static tail reference");
        const double drift = 10 * std::log10(energy(later, tail) / referenceTail);
        const double totalDrift = 10 * std::log10(energy(later) / energy(baseline));
        const double initialDrift = 10 * std::log10(energy(first, tail) / referenceTail);
        const double rtRatio = decay500(later, rate) / t500;
        const double initialRtRatio = decay500(first, rate) / t500;
        const double variation = difference(first, later, tail) / referenceTail;
        const double adjacentRms = std::sqrt(difference(later, adjacent, tail) / referenceTail);
        std::cout << "ROOM_MOTION type=" << type << " rate=" << rate
                  << " late_energy_db=" << drift << " initial_late_energy_db=" << initialDrift
                  << " total_energy_db=" << totalDrift << " rt500_ratio=" << rtRatio
                  << " initial_rt500_ratio=" << initialRtRatio
                  << " late_difference_energy_ratio=" << variation
                  << " difference_derivative_energy_ratio="
                  << differenceDerivative(first, later, tail) / referenceTail
                  << " adjacent_clock_difference_rms=" << adjacentRms << '\n';
        // Intent-derived bounds: the room remains the same room in level and
        // absorption, with a nonzero but not claimed-perceptual late variation.
        require(std::abs(drift) < .5 && std::abs(initialDrift) < .5 && std::abs(totalDrift) < .5,
                "room motion changes energy by more than half a dB");
        require(std::abs(rtRatio - 1) < .1 && std::abs(initialRtRatio - 1) < .1,
                "room motion changes 500Hz decay by more than ten percent");
        require(variation > 0, "elapsed room time does not vary the late tail");
        // A one-sample change in hit time must not switch the room abruptly.
        // -80dB relative RMS leaves margin for float roundoff and repeated FDN
        // circulations while excluding discontinuous coefficient replacement.
        require(adjacentRms < 1.e-4, "late response jumps between adjacent room-clock samples");
    }
}

void linearityAndTailReset() {
    constexpr double rate = 48000;
    for (int type = 0; type < 3; ++type) {
        const auto prototype = makeRoom(type, rate);
        RoomCapture a = *prototype, b = *prototype, combined = *prototype;
        // Superposition of a time-varying linear room must use the same clock
        // for every source, including before its first nonzero excitation.
        double reference = 0, error = 0, peak = 0;
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            const float xa = i == 0 ? .3f : 0.0f;
            const float xb = i == static_cast<int>(.137 * rate) ? -.7f : 0.0f, x = xa + xb;
            float al, ar, bl, br, l, r;
            a.Process(&xa, 1, al, ar); b.Process(&xb, 1, bl, br); combined.Process(&x, 1, l, r);
            for (const auto pair : {std::array<double, 2>{l, static_cast<double>(al) + bl},
                                    std::array<double, 2>{r, static_cast<double>(ar) + br}}) {
                const double d = pair[0] - pair[1]; error += d * d; reference += pair[1] * pair[1];
                peak = std::max(peak, std::abs(d));
            }
        }
        require(reference > 1.e-8 && error < reference * 1.e-10 && peak < 2.e-6,
                "moving room fails matched-clock linear superposition");

        RoomCapture sounding = *prototype, quiet = *prototype;
        const int elapsed = static_cast<int>(1.375 * rate);
        for (int i = 0; i < elapsed; ++i) {
            const float hit = i == 0 ? 1.0f : 0.0f, zero = 0;
            float l, r; sounding.Process(&hit, 1, l, r); quiet.Process(&zero, 1, l, r);
            require(l == 0 && r == 0, "quiet room emits during clock advance");
        }
        sounding.Reset(false); quiet.Reset(false);
        silence(sounding, 64); silence(quiet, 64);
        const auto retained = impulse(sounding, 1.0), silentClock = impulse(quiet, 1.0);
        require(retained == silentClock, "tail reset retains audio or signal-dependent motion state");
        RoomCapture reset = *prototype;
        const auto initial = impulse(reset, 1.0);
        require(difference(retained, initial) > 0, "tail reset rewinds the room's elapsed-time motion");
        sounding.Reset();
        require(impulse(sounding, 1.0) == initial, "full reset does not rewind room motion and audio");
        sounding.Reset(false); silence(sounding, static_cast<int>(rate));
        sounding.FlushTiny();
        require(sounding.HasNoSubnormalState(), "motion/reset leaves subnormal room state");
        std::cout << "ROOM_MOTION_LIFECYCLE type=" << type << " superposition_peak_error=" << peak
                  << " superposition_error_ratio=" << error / reference
                  << " retained_clock_difference_ratio=" << difference(retained, initial) / energy(initial)
                  << '\n';
    }
}
}

int main() {
    responseAndClock(); linearityAndTailReset();
    std::cout << "RoomMotionTest PASS checks=" << checks << '\n';
}
