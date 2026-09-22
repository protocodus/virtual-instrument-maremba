#include "MalletContact.h"
#include "ContactExcitation.h"
#include "MarembaVoice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static void LegacyContact(MarembaVoice& voice) {
        voice.mLocalContactActive = false;
        voice.mMalletContact.Reset();
    }
    static double HeadEnergy(const MarembaVoice& voice) { return voice.mMalletContact.Energy(); }
};
}
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
double energy(const std::array<double, 8>& velocity, const maremba::MalletContact& contact) {
    double result = contact.Energy();
    for (double v : velocity) result += v * v;
    return result;
}
constexpr double mobility[8] = {1, .3, .1, .04, .018, .008, .004, .001};
double singleModeLoss(int mode, double position, double width) {
    maremba::MalletContact contact;
    contact.Configure(8, position, width, 1.5, mobility);
    std::array<double, 8> velocity{};
    velocity[mode] = 1;
    contact.Process(velocity.data(), 1);
    return 1 - energy(velocity, contact);
}
struct Comparison { double relativeDifference, gainDb, residual, highBandDb, maxHead; };
Comparison compare(int model, double phase, double rate, bool retrigger = true) {
    maremba::MarembaVoice local;
    maremba::FastPRNG random(123u);
    auto strike = [&](maremba::MarembaVoice& voice, maremba::FastPRNG& prng, float velocity) {
        voice.Trigger(48, velocity, static_cast<maremba::EMarimbaModel>(model), .4f, .35f,
                      0, .7f, 4, 0, 0, 0, rate, prng, 0, 0, 1, 0);
    };
    strike(local, random, 1);
    if (retrigger) {
        const int wait = static_cast<int>((.04 + phase / 130.81278265) * rate);
        for (int n = 0; n < wait; ++n) {
            float c, f, p; local.ProcessSample(c, f, p);
        }
    }
    auto legacy = local;
    auto secondRandom = random;
    if (retrigger) { strike(local, random, .05f); strike(legacy, secondRandom, .05f); }
    maremba::MarembaVoiceTestAccess::LegacyContact(legacy);
    double localEnergy = 0, legacyEnergy = 0, difference = 0, cross = 0;
    double lowLocal = 0, lowLegacy = 0, highLocal = 0, highLegacy = 0, maxHead = 0;
    const double alpha = 1 - std::exp(-2 * 3.14159265358979323846 * 700 / rate);
    for (int n = 0; n < static_cast<int>(.1 * rate); ++n) {
        float a, af, ap, b, bf, bp;
        local.ProcessSample(a, af, ap); legacy.ProcessSample(b, bf, bp);
        require(std::isfinite(a) && std::isfinite(ap), "contact produced non-finite sound");
        localEnergy += a * a; legacyEnergy += b * b; cross += a * b;
        difference += (a - b) * (a - b);
        lowLocal += alpha * (a - lowLocal); lowLegacy += alpha * (b - lowLegacy);
        highLocal += (a - lowLocal) * (a - lowLocal);
        highLegacy += (b - lowLegacy) * (b - lowLegacy);
        maxHead = std::max(maxHead, maremba::MarembaVoiceTestAccess::HeadEnergy(local));
        if ((n & 31) == 31) { local.UpdateEnvelope(); legacy.UpdateEnvelope(); }
    }
    require(maremba::MarembaVoiceTestAccess::HeadEnergy(local) == 0, "mallet state survived separation");
    local.FastKill();
    require(maremba::MarembaVoiceTestAccess::HeadEnergy(local) == 0, "mallet state survived voice reset");
    return {difference / legacyEnergy, 10 * std::log10(localEnergy / legacyEnergy),
            std::max(0., 1 - cross * cross / (localEnergy * legacyEnergy)),
            10 * std::log10((highLocal / localEnergy) / (highLegacy / legacyEnergy)), maxHead};
}
}
int main() {
    double worstEnergyGrowth = 0, maximumTransfer = 0, maximumHead = 0;
    for (double position : {.05, .2242, .35, .5, .95})
        for (double width : {0., .025, .12}) for (double loss : {.1, .8, 2.5}) {
            maremba::MalletContact contact;
            contact.Configure(8, position, width, loss, mobility);
            std::array<double, 8> velocity{};
            velocity[0] = 1;
            for (int step = 0; step < 100; ++step) {
                const double before = energy(velocity, contact);
                const double fraction = maremba::ContactCumulative((step + 1) / 100., .2)
                                      - maremba::ContactCumulative(step / 100., .2);
                contact.Process(velocity.data(), fraction);
                const double after = energy(velocity, contact);
                worstEnergyGrowth = std::max(worstEnergyGrowth, after - before);
                require(after <= before + 1.e-13, "finite-mass contact injected modal/head energy");
                double transferred = 0;
                for (int m = 1; m < 8; ++m) transferred += velocity[m] * velocity[m];
                maximumTransfer = std::max(maximumTransfer, transferred);
                maximumHead = std::max(maximumHead, contact.Energy());
            }
        }
    require(maximumTransfer > 1.e-4 && maximumHead > 1.e-4,
            "contact has no spatial mode transfer or finite mallet inertia");
    const double oddCenterLoss = singleModeLoss(1, .5, .12);
    const double oddOffCenterLoss = singleModeLoss(1, .35, .12);
    require(oddCenterLoss > 1.e-4, "wide felt spanning an odd-mode node absorbed no energy");
    require(oddCenterLoss < .8 * oddOffCenterLoss, "pure odd mode lost its center-node protection");
    require(singleModeLoss(1, .5, 0) < 1.e-12, "point contact at exact odd node absorbed motion");
    require(singleModeLoss(0, .2242, .025) < .2 * singleModeLoss(0, .5, .025),
            "fundamental node did not protect the ringing bar");
    // A velocity port responds to phase, not just an amplitude envelope: a
    // displacement extremum (zero velocity) cannot feed this instantaneous loss.
    maremba::MalletContact turningPoint, crossing;
    turningPoint.Configure(8, .35, .06, 1, mobility);
    crossing.Configure(8, .35, .06, 1, mobility);
    std::array<double, 8> zero{}, moving{}; moving[0] = 1;
    turningPoint.Process(zero.data(), .2); crossing.Process(moving.data(), .2);
    require(energy(zero, turningPoint) == 0 && energy(moving, crossing) < .95,
            "contact is insensitive to instantaneous modal velocity");
    std::cout << "PASSIVITY growth=" << worstEnergyGrowth << " cross_mode_energy=" << maximumTransfer
              << " head_energy=" << maximumHead << " odd_center_loss=" << oddCenterLoss
              << " odd_offcenter_loss=" << oddOffCenterLoss << '\n';

    for (int model = 0; model < 3; ++model) {
        require(compare(model, 0, 96000, false).relativeDifference == 0,
                "local correction changed a fresh hit on a resting bar");
        double smallest = 1e10, largest = 0, minimumGain = 1e10, maximumGain = -1e10;
        double maximumResidual = 0, maximumHighBand = -1e10, head = 0;
        for (int phase = 0; phase < 16; ++phase) {
            const auto result = compare(model, phase / 16., 96000);
            smallest = std::min(smallest, result.relativeDifference);
            largest = std::max(largest, result.relativeDifference);
            minimumGain = std::min(minimumGain, result.gainDb);
            maximumGain = std::max(maximumGain, result.gainDb);
            maximumResidual = std::max(maximumResidual, result.residual);
            maximumHighBand = std::max(maximumHighBand, result.highBandDb);
            head = std::max(head, result.maxHead);
        }
        require(largest > .005 && maximumResidual > .002,
                "local restrike physics is inaudible or only a level change");
        require(largest > 2 * smallest, "restrike correction does not respond to ringing phase");
        require(maximumHighBand < 4, "contact redistributed excessive energy into a bright transient");
        require(head > 1.e-8, "voice never stored physical contact perturbation energy");
        const auto reference = compare(model, 0, 96000);
        double rateError = 0;
        for (double rate : {88200., 192000.}) {
            const auto measured = compare(model, 0, rate);
            rateError = std::max(rateError, std::abs(10 * std::log10(
                measured.relativeDifference / reference.relativeDifference)));
        }
        require(rateError < .5, "local-contact contribution changes materially with sample rate");
        std::cout << "CONTACT model=" << model << " difference_db=" << 10 * std::log10(smallest)
                  << "," << 10 * std::log10(largest) << " gain_db=" << minimumGain << "," << maximumGain
                  << " max_gain_fit_residual=" << maximumResidual << " max_high_band_db=" << maximumHighBand
                  << " rate_error_db=" << rateError << '\n';
    }
    std::cout << "Mallet contact contracts passed.\n";
}
