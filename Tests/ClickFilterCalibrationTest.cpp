// The impact click and membrane-edge noise use resonant filters whose declared
// centre gain is unity. Check the actual voice coefficients independently via
// their complex transfer, including rate scaling and unchanged ringing poles.
#include "MarembaVoice.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static std::array<double, 3> Clack(const MarembaVoice& v) {
        return {v.mClackBpC, v.mClackBpS, v.mClackBpB};
    }
    static std::array<double, 3> Slap(const MarembaVoice& v) {
        return {v.mSlapC, v.mSlapS, v.mSlapB};
    }
    static double Fundamental(const MarembaVoice& v) { return v.mF0; }
};
}

int main() {
    using namespace maremba;
    using Complex = std::complex<double>;
    constexpr double pi = 3.14159265358979323846;
    double minimumGain = 1e9, maximumGain = 0, maximumResponse = 0, maximumPoleError = 0;
    int count = 0;
    for (int ri = 0; ri < 32; ++ri) {
        const double rate = 88200 * std::pow(1536000. / 88200, ri / 31.);
        for (int model = 0; model < 4; ++model) for (int note : {36, 48, 60, 72, 84, 108}) {
            MarembaVoice voice;
            FastPRNG random(123);
            voice.Trigger(note, .5f, EMarimbaModel(model), .5f, .5f, 0, .7f, 1, 0, 0, 0,
                          rate, random, 0, 0, 1, 0);
            const auto spec = GetModelParams(EMarimbaModel(model));
            for (int kind = 0; kind < 2; ++kind) {
                const auto coefficients = kind ? MarembaVoiceTestAccess::Slap(voice)
                                               : MarembaVoiceTestAccess::Clack(voice);
                const double centre = kind ? 2600 : std::clamp(spec.clackFrequency
                    + .35 * MarembaVoiceTestAccess::Fundamental(voice), 2500., 9000.);
                const double bandwidth = kind ? 3200 : spec.clackBandwidth;
                const double frequency = std::min(centre, rate * .45);
                const double w = 2 * pi * frequency / rate;
                const double q = std::max(.3, frequency / std::max(bandwidth, 1.));
                const double r = std::clamp(std::exp(-pi * frequency / (q * rate)), 0., .9999);
                const auto response = [&coefficients](double omega) {
                    const Complex z = std::exp(Complex(0, -omega));
                    return std::abs(coefficients[2]
                        / (1. - coefficients[0] * z + coefficients[1] * z * z));
                };
                const double gain = response(w);
                // A damped all-pole resonator's actual peak is slightly below
                // the nominal centre. Unity at the centre is not a hard bound
                // of 1 on the entire frequency response; measure that separately.
                const double peakOmega = std::acos(std::clamp(coefficients[0]
                    * (1 + coefficients[1]) / (4 * coefficients[1]), -1., 1.));
                const double peak = response(peakOmega);
                const double poleError = std::max(std::abs(coefficients[0] - 2 * r * std::cos(w)),
                                                  std::abs(coefficients[1] - r * r));
                minimumGain = std::min(minimumGain, gain);
                maximumGain = std::max(maximumGain, gain);
                maximumResponse = std::max(maximumResponse, peak);
                maximumPoleError = std::max(maximumPoleError, poleError);
                ++count;
                if (!std::isfinite(gain) || !std::isfinite(peak) || std::abs(gain - 1) > 2e-8
                    || poleError > 1e-12 || peak < gain - 1e-10 || peak > 1.05) {
                    std::cerr << "FAIL model=" << model << " note=" << note << " rate=" << rate
                              << " filter=" << kind << " centre_gain=" << gain
                              << " peak_gain=" << peak << " pole_error=" << poleError << '\n';
                    return EXIT_FAILURE;
                }
            }
        }
    }
    std::cout << std::setprecision(12) << "PASS actual_voice_filters=" << count
              << " centre_gain_min=" << minimumGain << " centre_gain_max=" << maximumGain
              << " maximum_response=" << maximumResponse
              << " maximum_pole_error=" << maximumPoleError << '\n';
}
