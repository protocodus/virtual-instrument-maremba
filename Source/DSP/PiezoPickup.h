#pragma once

#include "DspMath.h"
#include "MarimbaModel.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// A contact transducer attached to the instrument, before the external preamp.
// This is a voiced mechanical/electrical approximation, not a measurement of a
// named pickup. The input is local bending strain, NOT a mix of the air mics.
// Two broad mounting modes represent the rail/bridge and bonded pickup patch;
// the RC load removes static strain and the adhesive limits high frequencies.
class PiezoPickup {
public:
    PiezoPickup() { Configure(kReferenceRate, EMarimbaModel::ImperialRosewood); }

    // Control rate, allocation-free. Preserve state on an ordinary restrike;
    // Reset when a voice is reused for a different bar or its rate changes.
    void Configure(double sampleRate, EMarimbaModel model) {
        const double fs = std::isfinite(sampleRate) && sampleRate >= 8000.0
            ? std::min(sampleRate, 1536000.0) : kReferenceRate;
        struct Mount {
            double bodyHz, bodyQ, patchHz, patchQ, loadHz, adhesiveHz;
            double bodyMix, patchMix, directMix, forceMix, outputGain;
        };
        Mount mount;
        switch (model) {
            case EMarimbaModel::MayanPadauk:
                mount = {235.0, 1.05, 920.0, 1.55, 100.0, 4900.0,
                         0.58, 0.72, 0.69, 0.26, 0.78};
                break;
            case EMarimbaModel::BalafonAncestral:
                mount = {310.0, 0.85, 1540.0, 2.10, 125.0, 6800.0,
                         0.66, 1.05, 0.64, 0.32, 0.90};
                break;
            case EMarimbaModel::KalimbaArtisan:
                mount = {420.0, 1.45, 2260.0, 2.35, 48.0, 8900.0,
                         0.44, 0.62, 0.74, 0.19, 0.78};
                break;
            case EMarimbaModel::ImperialRosewood:
            default:
                mount = {175.0, 1.15, 1180.0, 1.80, 72.0, 6400.0,
                         0.46, 0.68, 0.72, 0.22, 0.96};
                break;
        }
        mBody.Configure(fs, mount.bodyHz, mount.bodyQ);
        mPatch.Configure(fs, mount.patchHz, mount.patchQ);
        mBodyMix = mount.bodyMix;
        mPatchMix = mount.patchMix;
        mDirectMix = mount.directMix;
        mForceMix = mount.forceMix;
        mOutputGain = mount.outputGain;

        // Bilinear RC sections: a piezo's capacitance and finite input load
        // form a high-pass, while the adhesive/sensor mass soften fast strain.
        const double hp = std::tan(kPiD * mount.loadHz / fs);
        mHpPole = (1.0 - hp) / (1.0 + hp);
        mHpGain = 1.0 / (1.0 + hp);
        const double lp = std::tan(kPiD * std::min(mount.adhesiveHz, fs * 0.38) / fs);
        mLpPole = (1.0 - lp) / (1.0 + lp);
        mLpGain = lp / (1.0 + lp);
    }

    void Reset() {
        mBody.Reset();
        mPatch.Reset();
        mHpInput = mHpOutput = mLpInput = mLpOutput = 0.0;
    }

    // strain: signed sum of the modal motions at the mounting patch, using
    // ModalStrainWeight below. impactForce: the contact-force density (remove
    // the integrated excitation's dt before feeding it here), including any
    // physical knocks. structuralBody: rail/bridge motion, never cavity air.
    // All three inputs are nominal audio-scale quantities, with peaks < 1.
    double Process(double strain, double impactForce, double structuralBody) {
        const double body = mBody.Process(0.24 * strain + 0.52 * structuralBody
                                         + 0.32 * impactForce);
        const double patch = mPatch.Process(0.60 * strain + 0.12 * structuralBody
                                           + 0.78 * impactForce);
        const double charge = mDirectMix * strain + 0.10 * structuralBody
                            + mForceMix * impactForce + mBodyMix * body + mPatchMix * patch;
        const double hp = mHpGain * (charge - mHpInput) + mHpPole * mHpOutput;
        mHpInput = charge;
        mHpOutput = hp;
        const double lp = mLpGain * (hp + mLpInput) + mLpPole * mLpOutput;
        mLpInput = hp;
        mLpOutput = lp;
        return mOutputGain * lp;
    }

    // A fixed adhesive footprint reads the spatially averaged curvature of a
    // free bar (three mallet instruments) or clamped tine (Kalimba). Curvature
    // is the second derivative of displacement: higher modes can produce much
    // more contact voltage than radiated sound, and its sign changes at nodes.
    // Use once per mode at trigger, never inside the sample loop. Frequency
    // ratios stay independently voiced in MarimbaModel; this uniform-beam
    // geometry approximates the mounting response, not an undercut-bar FEM.
    static double ModalStrainWeight(EMarimbaModel model, int mode) {
        struct BeamMode { double root, sigma; };
        static constexpr BeamMode bar[8] = {
            {4.7300407448627, 0.982502214576238}, {7.85320462409584, 1.00077731190727},
            {10.9956078380017, 0.999966450125409}, {14.137165491257, 1.00000144989766},
            {17.278759657399, 0.999999937344383}, {20.420352245626, 1.0000000027076},
            {23.56194490204, 0.999999999882994}, {26.7035375555, 1.00000000000506}
        };
        static constexpr BeamMode cantilever[8] = {
            {1.87510406871196, 0.734095513758913}, {4.69409113297417, 1.01846731875922},
            {7.85475743823761, 0.999224496517428}, {10.9955407348755, 1.00003355325171},
            {14.1371683910465, 0.99999855010865}, {17.2787595320882, 1.00000006265563},
            {20.4203522510413, 0.999999997292405}, {23.5619449018064, 1.00000000011701}
        };
        const bool tine = model == EMarimbaModel::KalimbaArtisan;
        double position = 0.24, width = 0.10;
        if (model == EMarimbaModel::MayanPadauk) { position = 0.29; width = 0.13; }
        if (model == EMarimbaModel::BalafonAncestral) { position = 0.20; width = 0.09; }
        if (tine) { position = 0.06; width = 0.075; }
        mode = std::max(mode, 0);
        // Above the tabulated roots both families approach sigma = 1 and
        // roots spaced by pi: free-free (m + 1.5) pi, cantilever (m + 0.5) pi.
        const BeamMode b = mode < 8 ? (tine ? cantilever[mode] : bar[mode])
            : BeamMode{(mode + (tine ? 0.5 : 1.5)) * kPiD, 1.0};
        const double beta0 = tine ? cantilever[0].root : bar[0].root;
        const double z = b.root * position;
        const double h = b.root * width * 0.5;
        const double hyperbolic = 0.5 * ((1.0 - b.sigma) * std::exp(z)
                                     + (1.0 + b.sigma) * std::exp(-z));
        const double circular = std::cos(z) - b.sigma * std::sin(z);
        const double hyperbolicAverage = (std::exp(h) - std::exp(-h)) / (2.0 * h);
        const double circularAverage = std::sin(h) / h;
        const double curvature = hyperbolic * hyperbolicAverage
                               + (tine ? 1.0 : -1.0) * circular * circularAverage;
        // Consistent polarity with ModalContactWeight's spatial convention;
        // orient the bar transducer so its fundamental is electrically positive.
        const double orientation = tine || (mode / 2) % 2 == 0 ? 1.0 : -1.0;
        const double normalization = tine ? 0.5 : 0.70710678118655;
        return orientation * normalization * curvature * (b.root * b.root) / (beta0 * beta0);
    }

    // Control rate: include this in the voice's finite/lifecycle guard. These
    // are short, damped mounting tails, but their last samples must not be cut.
    double StateEnergy() const {
        return mBody.StateEnergy() + mPatch.StateEnergy()
             + mHpInput * mHpInput + mHpOutput * mHpOutput
             + mLpInput * mLpInput + mLpOutput * mLpOutput;
    }

    void FlushTiny() {
        mBody.FlushTiny();
        mPatch.FlushTiny();
        if (std::abs(mHpInput) < 1.0e-15 && std::abs(mHpOutput) < 1.0e-15
            && std::abs(mLpInput) < 1.0e-15 && std::abs(mLpOutput) < 1.0e-15) {
            mHpInput = mHpOutput = mLpInput = mLpOutput = 0.0;
        }
    }

    bool HasNoSubnormalState() const {
        return mBody.HasNoSubnormalState() && mPatch.HasNoSubnormalState()
            && std::fpclassify(mHpInput) != FP_SUBNORMAL
            && std::fpclassify(mHpOutput) != FP_SUBNORMAL
            && std::fpclassify(mLpInput) != FP_SUBNORMAL
            && std::fpclassify(mLpOutput) != FP_SUBNORMAL;
    }

private:
    struct MountMode {
        // Bandpass with unity centre gain and zero DC; passive resonances do
        // not amplify arbitrarily when the sample rate or damping changes.
        void Configure(double fs, double hz, double q) {
            const double w = kTwoPiD * std::min(hz, fs * 0.38) / fs;
            const double alpha = std::sin(w) / (2.0 * q);
            const double inverse = 1.0 / (1.0 + alpha);
            b0 = alpha * inverse;
            a1 = -2.0 * std::cos(w) * inverse;
            a2 = (1.0 - alpha) * inverse;
        }
        double Process(double input) {
            const double out = b0 * input + z1;
            z1 = -a1 * out + z2;
            z2 = -b0 * input - a2 * out;
            return out;
        }
        void Reset() { z1 = z2 = 0.0; }
        double StateEnergy() const { return z1 * z1 + z2 * z2; }
        void FlushTiny() {
            if (std::abs(z1) < 1.0e-15 && std::abs(z2) < 1.0e-15) Reset();
        }
        bool HasNoSubnormalState() const {
            return std::fpclassify(z1) != FP_SUBNORMAL && std::fpclassify(z2) != FP_SUBNORMAL;
        }
        double b0 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

    MountMode mBody, mPatch;
    double mBodyMix = 0.0, mPatchMix = 0.0, mDirectMix = 0.0;
    double mForceMix = 0.0, mOutputGain = 1.0;
    double mHpPole = 0.0, mHpGain = 0.0, mLpPole = 0.0, mLpGain = 0.0;
    double mHpInput = 0.0, mHpOutput = 0.0, mLpInput = 0.0, mLpOutput = 0.0;
};

} // namespace maremba
