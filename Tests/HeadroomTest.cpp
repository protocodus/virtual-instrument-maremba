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
constexpr std::array<int, 4> kGroupStart{0, 2, 4, 6};
constexpr std::array<int, 4> kGroupSize{2, 2, 2, 1};

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
            if (quiet)
                require(output[i][c] == expected[c], "quiet signal changed beyond its delay");
            const double peak = std::abs(static_cast<double>(output[i][c]));
            maximumPeak = std::max(maximumPeak, peak);
            require(peak <= static_cast<double>(OutputHeadroom::kCeiling) + 1.5e-7,
                    "lookahead failed to catch an arbitrary transient on one of seven outputs");
        }
        if (!quiet) for (int group = 0; group < 3; ++group) {
            // Each stereo pair shares gain; none of the other captures does.
            const int left = kGroupStart[group], right = left + 1;
            const double cross = static_cast<double>(output[i][left]) * expected[right]
                               - static_cast<double>(output[i][right]) * expected[left];
            const double scale = std::max(1.0, std::abs(static_cast<double>(expected[left]))
                                              + std::abs(static_cast<double>(expected[right])));
            require(std::abs(cross) < 2e-7 * scale, "stereo capture received different channel gains or delays");
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
            input[i][c] = randomSigned(seed) * 0.25f;
    }
    OutputHeadroom processor;
    processor.SetSampleRate(rate);
    const Audio first = process(processor, input, 64);
    verifyDelay(input, first, delay, true);
    // Contaminate audio, gain and minimum history, then prove Reset clears all
    // of them in every group. Independent loud stems are exercised below.
    for (int i = 0; i < 3 * delay; ++i) {
        Frame loud {100.0f, -300.0f, 200, -150, 75, -100, 500};
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
            input[i][c] = randomSigned(seed) * amplitude;
        if (i % (delay + 7) == 0) input[i][i % 7] = (i % 3 ? -1 : 1) * 1000.0f;
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
    double xx = 0;
    std::array<double, 4> xy{}, yy{}, peak{};
    // Fit a gain to the delayed sine after one second of settling. This
    // measures added components, not an apparent error caused by latency.
    for (int i = 0; i < static_cast<int>(2 * rate); ++i) {
        const float x = static_cast<float>(2.0 * std::sin(2 * pi * frequency * i / rate));
        Frame frame {x, -0.5f * x, x, -0.5f * x, x, -0.5f * x, x};
        processor.Process(frame);
        if (i < rate) continue;
        const float delayed = static_cast<float>(2.0 * std::sin(2 * pi * frequency * (i - delay) / rate));
        xx += static_cast<double>(delayed) * delayed;
        for (int group = 0; group < 4; ++group) {
            const int channel = kGroupStart[group];
            xy[group] += static_cast<double>(delayed) * frame[channel];
            yy[group] += static_cast<double>(frame[channel]) * frame[channel];
            peak[group] = std::max(peak[group], std::abs(static_cast<double>(frame[channel])));
            require(frame[channel] == frame[0], "equal overloads received unequal group protection");
            if (group < 3) require(frame[channel + 1] == -.5f * frame[channel],
                                   "steady stereo overload changed balance or polarity");
        }
    }
    for (int group = 0; group < 4; ++group) {
        const double gain = xy[group] / xx;
        const double residual = std::sqrt(std::max(0.0, yy[group] - xy[group] * xy[group] / xx) / yy[group]);
        require(peak[group] <= static_cast<double>(OutputHeadroom::kCeiling) + 1.5e-7, "steady overload exceeds ceiling");
        require(gain > 0.45 && gain < 0.60, "steady overload lost excessive level or fails to attenuate");
        // The gain hold must span bass half-cycles, not merely the lookahead.
        // Otherwise recovery between successive peaks modulates even a sustained
        // 20 Hz sine. Fractional frequencies avoid relying on repeated sample phases.
        std::cout << "STEADY rate=" << rate << " frequency=" << frequency << " gain=" << gain
                  << " group=" << group << " peak=" << peak[group] << " residual=" << residual
                  << " residual_db=" << 20 * std::log10(std::max(residual, 1e-15)) << '\n';
        require(residual < 1e-4, "held overload gain adds steady-waveform components above -80 dB");
    }
}

void independentGroups(double rate) {
    const int delay = OutputHeadroom::LatencyFrames(rate);
    Audio quiet(static_cast<std::size_t>(rate * .08) + 2 * delay);
    std::uint32_t seed = 0x892fc11u;
    for (std::size_t i = 0; i + 2 * delay < quiet.size(); ++i)
        for (float& sample : quiet[i]) sample = .15f * randomSigned(seed);
    OutputHeadroom processor;
    processor.SetSampleRate(rate);
    const Audio quietOutput = process(processor, quiet, 64);
    for (int group = 0; group < 4; ++group) {
        context = "independent group=" + std::to_string(group) + " rate=" + std::to_string(rate);
        Audio overloaded = quiet;
        const int first = kGroupStart[group], end = first + kGroupSize[group];
        for (std::size_t i = 0; i + 2 * delay < overloaded.size(); ++i)
            for (int c = first; c < end; ++c)
                overloaded[i][c] = (i % (delay + 1) == 0 ? 1000.f : 8.f) * randomSigned(seed);
        processor.Reset();
        const Audio output = process(processor, overloaded, 17);
        verifyDelay(overloaded, output, delay, false);
        double audiblePeak = 0;
        for (std::size_t i = 0; i < output.size(); ++i) for (int c = 0; c < 7; ++c) {
            if (c < first || c >= end)
                require(output[i][c] == quietOutput[i][c], "one capture's overload ducks another capture");
            else audiblePeak = std::max(audiblePeak, std::abs(static_cast<double>(output[i][c])));
        }
        require(audiblePeak > .1, "overload protection mutes the capture");
    }
}

void simultaneousIndependentGroups() {
    context = "simultaneous independent group envelopes";
    constexpr int rate = 48000;
    OutputHeadroom combined;
    std::array<OutputHeadroom, 4> isolated;
    combined.SetSampleRate(rate);
    for (auto& processor : isolated) processor.SetSampleRate(rate);
    const int delay = OutputHeadroom::LatencyFrames(rate);
    std::array<bool, 4> reduced{};
    for (int i = 0; i < rate; ++i) {
        Frame input{.1f, -.2f, .15f, -.1f, .12f, -.08f, .1f};
        // Overlapping overloads have different levels and release times.
        // A single shared envelope can pass equal-overload tests, so compare
        // each group to a processor that has never seen any other capture.
        if (i >= 480 && i < 1440) input[1] = -20.f;  // Main right only
        if (i >= 960 && i < 4800) input[2] = 3.f;    // Close left only
        if (i >= 1920 && i < 2880) input[5] = -8.f;  // Room right only
        if (i >= 2400 && i < 7200) input[6] = 1.5f;  // mono Piezo
        Frame actual = input;
        combined.Process(actual);
        for (int group = 0; group < 4; ++group) {
            Frame expected{};
            const int first = kGroupStart[group], end = first + kGroupSize[group];
            for (int c = first; c < end; ++c) expected[c] = input[c];
            isolated[group].Process(expected);
            for (int c = first; c < end; ++c) {
                require(actual[c] == expected[c],
                        "simultaneous overloads share gain or recovery across captures");
                require(std::abs(actual[c]) <= OutputHeadroom::kCeiling + 1.5e-7f,
                        "simultaneous overload escapes protection");
            }
            // Each stereo overload must also reduce its quiet sibling, even
            // when only the right channel drives the detector. Piezo has no
            // sibling; its sustained overload must itself be attenuated.
            const int quietSibling = group == 1 ? 3 : first;
            const float quietValue = group == 0 ? .1f : group == 1 ? -.1f : .12f;
            const int start = group == 0 ? 480 : group == 1 ? 960 : group == 2 ? 1920 : 2400;
            const int stop = group == 0 ? 1440 : group == 1 ? 4800 : group == 2 ? 2880 : 7200;
            if (i >= start + delay && i < stop) {
                const bool isReduced = group < 3 ? std::abs(actual[quietSibling]) < std::abs(quietValue) * .9f
                                                 : actual[6] < 1.0f;
                reduced[group] = reduced[group] || isReduced;
            }
        }
    }
    for (bool active : reduced) require(active, "concurrent fixture did not limit every group and stereo sibling");
}

void independentDirectReset() {
    const Frame quiet {.1f, -.2f, .3f, -.4f, .5f, -.6f, .7f};
    for (int direct = 0; direct < 5; ++direct) {
        context = "independent direct reset index=" + std::to_string(direct);
        OutputHeadroom processor;
        processor.SetSampleRate(48000);
        const int delay = OutputHeadroom::LatencyFrames(48000);
        for (int i = 0; i < delay; ++i) {
            Frame frame = quiet;
            processor.Process(frame);
        }
        processor.ResetDirect(direct);
        Frame frame{};
        processor.Process(frame);
        Frame expected = quiet; expected[direct + 2] = 0;
        require(frame == expected, "resetting one direct output corrupts another delay");

        // Reconnecting one side must clear its queued audio, without exposing
        // hot queued audio on its still-connected stereo sibling at unity gain.
        // Gain/minimum history is intentionally retained, including for Piezo.
        processor.Reset();
        OutputHeadroom reference;
        reference.SetSampleRate(48000);
        const int target = direct + 2;
        const int group = target < 4 ? 1 : target < 6 ? 2 : 3;
        const int first = kGroupStart[group], end = first + kGroupSize[group];
        Frame loud = quiet;
        for (int c = first; c < end; ++c) loud[c] = c % 2 ? -100.f : 300.f;
        for (int i = 0; i < 3 * delay; ++i) {
            Frame a = loud, b = loud; processor.Process(a); reference.Process(b);
        }
        processor.ResetDirect(direct);
        double afterDelay = 0, last = 0;
        for (int i = 0; i < 48000; ++i) {
            Frame a = quiet, b = quiet; processor.Process(a); reference.Process(b);
            for (int c = 0; c < 7; ++c) {
                require(std::isfinite(a[c]) && std::abs(a[c]) <= OutputHeadroom::kCeiling + 1.5e-7f,
                        "reconnecting an output leaked an over-ceiling sibling or sample");
                if (c < first || c >= end) require(a[c] == b[c], "direct reset changes another group");
                if (i < delay && c == target) require(a[c] == 0, "old disconnected samples replay on reconnect");
                if (i < delay && c >= first && c < end && c != target)
                    require(std::abs(a[c]) > .1f, "direct reset erased the connected stereo sibling");
            }
            if (i == delay) afterDelay = a[target] / quiet[target];
            last = a[target] / quiet[target];
        }
        require(afterDelay < .1, "reconnect discarded still-active overload history");
        require(last > .9999, "reconnected quiet capture fails to recover naturally");
    }
}

void pendingFirReset() {
    // A .45-amplitude alternating run with one repeated sign reconstructs
    // above the ceiling. Its large FIR estimate arrives only after the input
    // run, so this fixture distinguishes clearing FIR history from clearing
    // just queued audio. The unreset reference must actually reduce gain.
    for (int direct = 0; direct < 5; ++direct) {
        context = "pending FIR history reset index=" + std::to_string(direct);
        OutputHeadroom processor, reference;
        processor.SetSampleRate(48000); reference.SetSampleRate(48000);
        const int delay = OutputHeadroom::LatencyFrames(48000);
        const int target = direct + 2;
        Frame baseline{.1f, .1f, .1f, .1f, .1f, .1f, .1f};
        baseline[target] = 0;
        for (int i = 0; i < 2 * delay + 32; ++i) {
            Frame a = baseline, b = baseline; processor.Process(a); reference.Process(b);
        }
        for (int i = 0; i < 24; ++i) {
            Frame a = baseline, b = baseline;
            a[target] = b[target] = .45f * ((i < 16 ? i % 2 : !(i % 2)) ? 1 : -1);
            processor.Process(a); reference.Process(b);
            for (int c = 0; c < 7; ++c) if (c != target)
                require(a[c] == .1f, "pending-FIR fixture engaged protection before reset");
        }
        processor.ResetDirect(direct);
        bool referenceReduced = false;
        for (int i = 0; i < 3 * delay; ++i) {
            Frame a{.1f, .1f, .1f, .1f, .1f, .1f, .1f}, b = a;
            processor.Process(a); reference.Process(b);
            for (int c = 0; c < 7; ++c)
                require(a[c] == (c == target && i < delay ? 0.f : .1f),
                        "reset retained stale FIR history or altered another channel");
            if (i >= delay && b[target] < .095f) referenceReduced = true;
        }
        require(referenceReduced, "pending-FIR fixture did not exercise delayed overload detection");
    }
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
                input[i] = {x, -x, x, -.5f * x, .2f * x, -.75f * x, x};
            }
            OutputHeadroom processor;
            processor.SetSampleRate(rate);
            verifyDelay(input, process(processor, input, 64), static_cast<int>(std::ceil(rate * .002)), true);
        }
    }
}

void smoothAttackAndRecovery(int group) {
    context = "smooth attack and recovery group=" + std::to_string(group);
    const int channel = kGroupStart[group];
    OutputHeadroom processor;
    processor.SetSampleRate(48000);
    const int delay = OutputHeadroom::LatencyFrames(48000);
    std::vector<float> input(48000, .1f);
    input[960] = 100.0f;
    double previousGain = 1;
    for (std::size_t i = 0; i < input.size(); ++i) {
        Frame frame{};
        frame[channel] = input[i];
        if (kGroupSize[group] == 2) frame[channel + 1] = -input[i] * .5f;
        processor.Process(frame);
        if (i < static_cast<std::size_t>(delay)) continue;
        const double gain = frame[channel] / static_cast<double>(input[i - delay]);
        require(std::abs(gain - previousGain) <= 1.0 / OutputHeadroom::SmoothingFrames(48000) + 2e-7,
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
        Frame frame{};
        frame[channel] = i < 480 ? 100.0f : 1.2f;
        processor.Process(frame);
        last = frame[channel];
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
        independentGroups(rate);
    }
    for (double rate : {22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
        for (double frequency : {20.0, 50.3, 100.1, 1000.0, 4000.3})
            steadyOverload(rate, frequency);
    independentDirectReset();
    simultaneousIndependentGroups();
    pendingFirReset();
    quietTones();
    for (int group = 0; group < 4; ++group) smoothAttackAndRecovery(group);
    std::cout << "PASS HeadroomTest checks=" << checks << " maximum_peak=" << maximumPeak << '\n';
}
