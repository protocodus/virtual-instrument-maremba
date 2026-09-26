// Output protection must preserve quiet audio exactly, give every output the
// same latency, and control a transient before its waveform reaches the output.
// These tests exercise the public processor independently of the instrument.
#include "OutputHeadroom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using maremba::OutputHeadroom;
using Frame = std::array<float, 7>;
using Audio = std::vector<Frame>;
constexpr double pi = 3.14159265358979323846;
std::uint64_t checks = 0;
std::string context;
double maximumPeak = 0.0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL " << context << ": " << message << '\n';
        std::exit(1);
    }
}

std::uint32_t randomBits(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomSigned(std::uint32_t& state) {
    return static_cast<float>(randomBits(state) >> 8) / 8388608.0f - 1.0f;
}

Audio process(OutputHeadroom& processor, const Audio& input, int blockSize) {
    Audio output = input;
    for (std::size_t block = 0; block < output.size(); block += blockSize)
        for (std::size_t i = block; i < std::min(output.size(), block + blockSize); ++i)
            processor.Process(output[i]);
    return output;
}

void verifyDelay(const Audio& input, const Audio& output, int delay, bool quiet) {
    require(input.size() == output.size(), "wrong output frame count");
    for (std::size_t i = 0; i < output.size(); ++i) {
        const Frame expected = i < static_cast<std::size_t>(delay) ? Frame{} : input[i - delay];
        for (int c = 0; c < 7; ++c) {
            require(std::isfinite(output[i][c]), "nonfinite output");
            if (c >= 2 || quiet)
                require(output[i][c] == expected[c], "quiet signal or independent stem changed beyond its delay");
        }
        const double peak = std::max(std::abs(output[i][0]), std::abs(output[i][1]));
        maximumPeak = std::max(maximumPeak, peak);
        require(peak <= static_cast<double>(OutputHeadroom::kCeiling) + 1.5e-7,
                "lookahead failed to catch an arbitrary transient");
        if (!quiet) {
            // A common gain must preserve polarity, stereo balance and nulls.
            const double cross = static_cast<double>(output[i][0]) * expected[1]
                               - static_cast<double>(output[i][1]) * expected[0];
            const double scale = std::max(1.0, std::abs(static_cast<double>(expected[0]))
                                              + std::abs(static_cast<double>(expected[1])));
            require(std::abs(cross) < 2e-7 * scale, "main channels received different gain or delay");
        }
    }
}

void quietAndReset(double rate) {
    context = "quiet/delay/reset rate=" + std::to_string(rate);
    const int delay = static_cast<int>(std::ceil(rate * 0.002));
    require(OutputHeadroom::LatencyFrames(rate) == delay, "latency is not the declared two milliseconds");
    Audio input(static_cast<std::size_t>(delay * 8 + 257));
    std::uint32_t seed = 0xdeadbeefu;
    for (std::size_t i = 0; i + delay < input.size(); ++i) {
        for (int c = 0; c < 7; ++c)
            input[i][c] = randomSigned(seed) * (c < 2 ? 0.25f : 12.0f);
    }
    OutputHeadroom processor;
    processor.SetSampleRate(rate);
    const Audio first = process(processor, input, 64);
    verifyDelay(input, first, delay, true);
    // Contaminate audio, gain and minimum history, then prove Reset clears all
    // of them. Large direct stems must never turn down a quiet main signal.
    for (int i = 0; i < 3 * delay; ++i) {
        Frame loud {100.0f, -300.0f, 1, 2, 3, 4, 5};
        processor.Process(loud);
    }
    processor.Reset();
    require(process(processor, input, 1) == first, "Reset does not replay quiet audio exactly");
    processor.SetSampleRate(rate == 48000 ? 192000 : 48000);
    Frame loud {300, -100, 5, 4, 3, 2, 1};
    processor.Process(loud);
    processor.SetSampleRate(rate);
    processor.Reset();
    require(process(processor, input, 23) == first, "rate change left stale gain or delayed audio");
}

void transients(double rate) {
    context = "adversarial bursts rate=" + std::to_string(rate);
    const int delay = static_cast<int>(std::ceil(rate * 0.002));
    Audio input(static_cast<std::size_t>(rate * 0.6) + 2 * delay);
    std::uint32_t seed = 0x13579bdfu;
    for (std::size_t i = 0; i + 2 * delay < input.size(); ++i) {
        // Abrupt isolated and consecutive opposite-polarity overloads, ramped
        // peaks, sustained overload, and silence cross every queue boundary.
        const int phase = static_cast<int>(i / (delay + 1)) % 6;
        float amplitude = phase == 0 ? 0.0f : phase == 1 ? 0.3f :
                          phase == 2 ? 300.0f : phase == 3 ? 2.0f :
                          phase == 4 ? static_cast<float>(i % (delay + 1)) : 0.9f;
        for (int c = 0; c < 7; ++c)
            input[i][c] = randomSigned(seed) * (c < 2 ? amplitude : 7.0f);
        if (i % (delay + 7) == 0) input[i][i % 2] = (i % 3 ? -1 : 1) * 1000.0f;
    }
    OutputHeadroom processor;
    processor.SetSampleRate(rate);
    const Audio reference = process(processor, input, 64);
    verifyDelay(input, reference, delay, false);
    for (int block : {1, 17, 63, 127}) {
        processor.Reset();
        require(process(processor, input, block) == reference, "processing depends on caller block size");
    }
    processor.Reset();
    for (int i = 0; i < delay * 2 + 1; ++i) {
        Frame zero{};
        processor.Process(zero);
        require(zero == Frame{}, "Reset leaked a previous delayed sample");
    }
}

void steadyOverload(double rate, double frequency) {
    const int delay = static_cast<int>(std::ceil(rate * 0.002));
    context = "steady overload rate=" + std::to_string(rate)
            + " frequency=" + std::to_string(frequency);
    OutputHeadroom processor;
    processor.SetSampleRate(rate);
    double xx = 0, xy = 0, yy = 0, peak = 0;
    // Fit a gain to the delayed sine after one second of settling. This
    // measures added components, not an apparent error caused by latency.
    for (int i = 0; i < static_cast<int>(2 * rate); ++i) {
        const float x = static_cast<float>(2.0 * std::sin(2 * pi * frequency * i / rate));
        Frame frame {x, -0.5f * x, x, x, x, x, x};
        processor.Process(frame);
        if (i < rate) continue;
        const float delayed = static_cast<float>(2.0 * std::sin(2 * pi * frequency * (i - delay) / rate));
        require(frame[2] == delayed, "steady direct stem lost its exact delay");
        xx += static_cast<double>(delayed) * delayed;
        xy += static_cast<double>(delayed) * frame[0];
        yy += static_cast<double>(frame[0]) * frame[0];
        peak = std::max(peak, std::abs(static_cast<double>(frame[0])));
    }
    const double gain = xy / xx;
    const double residual = std::sqrt(std::max(0.0, yy - xy * xy / xx) / yy);
    require(peak <= static_cast<double>(OutputHeadroom::kCeiling) + 1.5e-7, "steady overload exceeds ceiling");
    require(gain > 0.45 && gain < 0.60, "steady overload lost excessive level or fails to attenuate");
    // The gain hold must span bass half-cycles, not merely the lookahead.
    // Otherwise recovery between successive peaks modulates even a sustained
    // 20 Hz sine. Fractional frequencies avoid relying on repeated sample phases.
    std::cout << "STEADY rate=" << rate << " frequency=" << frequency << " gain=" << gain
              << " peak=" << peak << " residual=" << residual
              << " residual_db=" << 20 * std::log10(std::max(residual, 1e-15)) << '\n';
    require(residual < 1e-4, "held overload gain adds steady-waveform components above -80 dB");
}

void independentDirectReset() {
    context = "independent direct reset";
    OutputHeadroom processor;
    processor.SetSampleRate(48000);
    const int delay = OutputHeadroom::LatencyFrames(48000);
    for (int i = 0; i < delay; ++i) {
        Frame frame {.1f, -.2f, 1, 2, 3, 4, 5};
        processor.Process(frame);
    }
    processor.ResetDirect(2);
    Frame frame{};
    processor.Process(frame);
    require(frame == Frame{.1f, -.2f, 1, 2, 0, 4, 5}, "resetting one direct output corrupts another delay");
}

void quietTones() {
    // The detector may conservatively protect unsampled peaks of complex
    // near-ceiling noise. A sine whose actual amplitude is .8 must still pass
    // exactly, including phases that put no sample on the waveform's peak.
    for (double rate : {22050.0, 32000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        for (double frequency : {20.0, 50.3, 1000.0, 4000.3, rate * .25 + .3, rate * .49}) {
            context = "quiet tone rate=" + std::to_string(rate)
                    + " frequency=" + std::to_string(frequency);
            Audio input(static_cast<std::size_t>(rate * .12));
            for (std::size_t i = 0; i < input.size(); ++i) {
                const float x = static_cast<float>(.8 * std::sin(2 * pi * frequency * i / rate));
                input[i] = {x, -x, 1, 2, 3, 4, 5};
            }
            OutputHeadroom processor;
            processor.SetSampleRate(rate);
            verifyDelay(input, process(processor, input, 64), static_cast<int>(std::ceil(rate * .002)), true);
        }
    }
}

void smoothAttackAndRecovery() {
    context = "smooth attack and recovery from an exceptional peak";
    OutputHeadroom processor;
    processor.SetSampleRate(48000);
    const int delay = OutputHeadroom::LatencyFrames(48000);
    std::vector<float> input(48000, .1f);
    input[960] = 100.0f;
    double previousGain = 1;
    for (std::size_t i = 0; i < input.size(); ++i) {
        Frame frame {input[i], -input[i] * .5f, 0, 0, 0, 0, 0};
        processor.Process(frame);
        if (i < static_cast<std::size_t>(delay)) continue;
        const double gain = frame[0] / static_cast<double>(input[i - delay]);
        require(std::abs(gain - previousGain) <= 1.0 / (delay + 1) + 2e-7,
                "protection applies an abrupt samplewise gain step");
        previousGain = gain;
    }
    require(previousGain > .9999, "protection leaves quiet audio attenuated after recovery");
    // A large isolated peak must not pin its gain while a smaller sustained
    // overload follows. Renewing the hold merely because peak > ceiling would
    // never release in this case, making ordinary sound unnecessarily quiet.
    processor.Reset();
    float last = 0;
    for (int i = 0; i < 48000; ++i) {
        Frame frame {i < 480 ? 100.0f : 1.2f, 0, 0, 0, 0, 0, 0};
        processor.Process(frame);
        last = frame[0];
    }
    require(last > .935f && last <= OutputHeadroom::kCeiling + 1.5e-7f,
            "obsolete peak keeps a lesser sustained sound over-attenuated");
}
} // namespace

int main() {
    std::cout << std::setprecision(10);
    for (double rate : {22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        quietAndReset(rate);
        transients(rate);
    }
    for (double rate : {22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
        for (double frequency : {20.0, 50.3, 100.1, 1000.0, 4000.3})
            steadyOverload(rate, frequency);
    independentDirectReset();
    quietTones();
    smoothAttackAndRecovery();
    std::cout << "PASS HeadroomTest checks=" << checks << " maximum_peak=" << maximumPeak << '\n';
}
