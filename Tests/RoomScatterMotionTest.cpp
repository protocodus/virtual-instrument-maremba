#include "RoomScatterMotion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

namespace { bool counting = false; std::size_t allocations = 0; }
void* operator new(std::size_t size) {
    if (counting) ++allocations;
    if (void* pointer = std::malloc(std::max(size, std::size_t{1}))) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using maremba::RoomScatterMotion;
using Frame = std::array<float, 12>;
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
Frame basis() {
    Frame frame{};
    for (const auto pair : RoomScatterMotion::kIndices) frame[pair[0]] = 1.0f;
    return frame;
}
double norm(const Frame& frame) {
    double result = 0;
    for (float value : frame) result += static_cast<double>(value) * value;
    return result;
}
std::uint64_t hash(std::uint64_t value, const Frame& frame) {
    for (float sample : frame) {
        std::uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        value = (value ^ bits) * 1099511628211ull;
    }
    return value;
}

void energyAndSmoothness() {
    RoomScatterMotion motion;
    constexpr double rate = 48000.0;
    motion.Configure(rate, 0x73e5129u);
    double worstEnergy = 0, maximumAngle = 0, maximumSpeed = 0;
    std::array<double, RoomScatterMotion::kPairs> previous{};
    Frame initial = basis(), frame = initial;
    motion.Process(frame.data());
    require(frame == initial, "first scattering matrix is not the identity");
    for (int sample = 1; sample < static_cast<int>(rate * 30); ++sample) {
        frame = initial;
        motion.Process(frame.data());
        const double error = std::abs(norm(frame) / norm(initial) - 1.0);
        worstEnergy = std::max(worstEnergy, error);
        require(error < 3.e-7, "scattering matrix changes input energy");
        for (int pairIndex = 0; pairIndex < RoomScatterMotion::kPairs; ++pairIndex) {
            const auto pair = RoomScatterMotion::kIndices[static_cast<std::size_t>(pairIndex)];
            const double angle = std::atan2(frame[pair[1]], frame[pair[0]]);
            const double speed = std::abs(angle - previous[static_cast<std::size_t>(pairIndex)]) * rate;
            maximumAngle = std::max(maximumAngle, std::abs(angle));
            maximumSpeed = std::max(maximumSpeed, speed);
            require(std::abs(angle) < RoomScatterMotion::kMaximumAngleRadians,
                    "rotation leaves the declared subtle angular range");
            // Float coefficient rounding contributes up to ~.0003 rad/s at
            // this test rate; the continuous trajectory bound is .036 rad/s.
            require(speed < RoomScatterMotion::kMaximumAngularSpeed + .001,
                    "scattering trajectory steps or exceeds its speed bound");
            previous[static_cast<std::size_t>(pairIndex)] = angle;
        }
    }
    require(maximumAngle > .01 && maximumSpeed > .005, "scattering motion is inactive");
    // Basis energy alone could miss non-orthogonal columns: use mixed signed
    // vectors and high/low magnitudes as an independent norm observation.
    for (int sample = 0; sample < 10000; ++sample) {
        for (int i = 0; i < 12; ++i)
            frame[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(.17 * sample + i)
                * (sample % 3 == 0 ? 1.e-12 : sample % 3 == 1 ? 1.0 : 1.e12));
        const double before = norm(frame);
        motion.Process(frame.data());
        const double error = std::abs(norm(frame) / before - 1.0);
        worstEnergy = std::max(worstEnergy, error);
        require(error < 3.e-7, "mixed input loses orthogonality");
    }
    std::cout << "SCATTER energy_relative_error=" << worstEnergy << " maximum_angle=" << maximumAngle
              << " maximum_angular_speed=" << maximumSpeed << '\n';
}

std::uint64_t sequence(RoomScatterMotion& motion, int frames, bool irregularBatches) {
    std::uint64_t value = 1469598103934665603ull;
    int rendered = 0;
    while (rendered < frames) {
        const int batch = std::min(frames - rendered, irregularBatches ? 1 + rendered % 127 : 64);
        for (int i = 0; i < batch; ++i) {
            Frame frame = basis(); motion.Process(frame.data()); value = hash(value, frame);
        }
        rendered += batch;
    }
    return value;
}
void reproducibility() {
    RoomScatterMotion motion;
    motion.Configure(48000.0, 12345);
    const auto reference = sequence(motion, 480000, false);
    motion.Reset();
    require(sequence(motion, 480000, true) == reference, "reset or callback subdivision changes motion");
    motion.Configure(48000.0, 12346);
    require(sequence(motion, 480000, false) != reference, "different seed leaves the same trajectory");
    motion.Configure(48000.0, 12345);
    require(sequence(motion, 480000, false) == reference, "Configure fails to replay the declared seed");

    RoomScatterMotion quiet, sounding;
    quiet.Configure(48000.0, 9); sounding.Configure(48000.0, 9);
    for (int i = 0; i < 300000; ++i) {
        Frame zero{}, signal = basis(); quiet.Process(zero.data()); sounding.Process(signal.data());
        require(zero == Frame{}, "zero input creates room energy");
    }
    Frame a = basis(), b = basis(); quiet.Process(a.data()); sounding.Process(b.data());
    require(a == b, "motion depends on the input signal or stops during silence");
    require(a != basis(), "silent time did not advance the room motion");
}

void rateAndAllocation() {
    // Observe the same continuously seeded motion at common physical times,
    // well beyond several irregular segment boundaries for every pair.
    std::vector<Frame> reference;
    double largestDifference = 0;
    for (double rate : {44100., 48000., 88200., 96000., 192000.}) {
        RoomScatterMotion motion; motion.Configure(rate, 0x5bcaf13u);
        std::vector<Frame> observation;
        const int interval = static_cast<int>(rate / 10);
        for (int i = 0; i <= static_cast<int>(rate * 12); ++i) {
            Frame frame = basis(); motion.Process(frame.data());
            if (i % interval == 0) observation.push_back(frame);
        }
        if (reference.empty()) reference = observation;
        else {
            require(observation.size() == reference.size(), "rate trace duration differs");
            for (std::size_t i = 0; i < reference.size(); ++i) for (int lane = 0; lane < 12; ++lane)
                largestDifference = std::max(largestDifference,
                    std::abs(static_cast<double>(reference[i][static_cast<std::size_t>(lane)])
                             - observation[i][static_cast<std::size_t>(lane)]));
        }
    }
    require(largestDifference < 1.e-6, "motion trajectory depends on sample rate rather than seconds");
    allocations = 0; counting = true;
    RoomScatterMotion motion;
    for (double rate : {44100., 192000., std::numeric_limits<double>::quiet_NaN()}) {
        motion.Configure(rate, 0);
        for (int i = 0; i < 10000; ++i) { Frame frame = basis(); motion.Process(frame.data()); }
        motion.Reset();
    }
    counting = false;
    require(allocations == 0, "motion allocates during configuration, reset or audio processing");
    std::cout << "SCATTER cross_rate_maximum_coefficient_difference=" << largestDifference
              << " allocations=" << allocations << '\n';
}
} // namespace

int main() {
    energyAndSmoothness(); reproducibility(); rateAndAllocation();
    std::cout << "RoomScatterMotionTest PASSED checks=" << checks << '\n';
}
