// An active implicit contact must use the bar's current response after
// coupled note-on tuning or a live pitch change. Check force histories using
// an independent moving-surface equivalence, including retained head state.
#include "MarembaVoice.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace maremba {
struct MarembaVoiceTestAccess {
    static double Compliance(const MarembaVoice& v) { return v.mContactCompliance; }
    static MalletContact Contact(const MarembaVoice& v) { return v.mContact; }
    static double FreeSurface(const MarembaVoice& v) {
        double surface = -v.mContactCompliance * v.mSupportLoad;
        for (int i = 0; i < v.mNumModes; ++i) {
            const auto& mode = v.mModes[i];
            const double next = mode.idle ? 0.0 : mode.c * mode.y1 - mode.s * mode.y2;
            surface += mode.contact * mode.toDisplacement * next;
        }
        return surface;
    }
    static void DisableTexture(MarembaVoice& v) { v.mMallet.texture = 0.0; }
    static MalletContact FreshAfterTuning(const MarembaVoice& v) {
        auto setup = v.mMalletSetup;
        setup.compliance = v.mContactCompliance;
        MalletContact contact;
        contact.Start(setup, v.ContactDisplacement());
        return contact;
    }
};
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

// Increasing compliance by dC moves the surface by dC * appliedForce.
// Therefore a contact retaining its old compliance but receiving this extra
// surface displacement must reproduce the updated contact's force/history.
// This tests the physical implicit equation rather than inspecting its cache.
void standalone(double rate, double newCompliance, bool held) {
    maremba::MalletContact::Setup setup;
    setup.dt = 1.0 / rate;
    setup.velocity = 2.0;
    setup.compliance = 2.e-9;
    maremba::MalletContact contact, reference;
    contact.Start(setup, 0.0);
    if (held) contact.Hold(5.0, 2.0);
    const int change = static_cast<int>(.0002 * rate);
    double peak = 0.0, error = 0.0;
    for (int i = 0; i < static_cast<int>(.012 * rate); ++i) {
        if (i == change) {
            reference = contact;
            const auto before = contact;
            contact.SetCompliance(newCompliance);
            require(contact.Active() == before.Active() && contact.InContact() == before.InContact()
                    && contact.Force() == before.Force() && contact.Velocity() == before.Velocity()
                    && contact.Holding() == before.Holding(), "compliance change restarted contact state");
        }
        const double surface = 1.e-5 * std::sin(2.0 * maremba::kPiD * 262.0 * i / rate);
        const double force = contact.Step(surface);
        if (i >= change) {
            const double expected = reference.Step(surface + (newCompliance - setup.compliance) * force);
            peak = std::max(peak, std::abs(force));
            error = std::max(error, std::abs(force - expected));
        }
    }
    require(peak > 1.0 && error < peak * 2.e-10,
            "changed compliance violates equivalent moving-surface force law");
}

maremba::MarembaVoice voiceFor(int model, int note, double rate) {
    maremba::MarembaVoice voice;
    maremba::FastPRNG prng(123);
    voice.Trigger(note, 80.f / 127, static_cast<maremba::EMarimbaModel>(model),
                  .4f, .5f, 0, .7f, 1, 0, 0, 0, rate, prng, 0, 0, 1, 0);
    maremba::MarembaVoiceTestAccess::DisableTexture(voice);
    return voice;
}

void noteOn(int model, int note, double rate) {
    auto voice = voiceFor(model, note, rate);
    auto expected = maremba::MarembaVoiceTestAccess::FreshAfterTuning(voice);
    double peak = 0.0, error = 0.0;
    for (int i = 0; i < static_cast<int>(.008 * rate); ++i) {
        const double surface = maremba::MarembaVoiceTestAccess::FreeSurface(voice);
        const double force = expected.Step(surface);
        float close, far, piezo;
        voice.ProcessSample(close, far, piezo);
        peak = std::max(peak, std::abs(force));
        error = std::max(error, std::abs(voice.GetContactForce() - force));
    }
    require(peak > 1.0 && error < peak * 1.e-12,
            "initial contact still uses compliance from before coupled tuning");
}

void bend(int model, int note, double rate, float cents) {
    auto voice = voiceFor(model, note, rate);
    for (int i = 0; i < static_cast<int>(.00015 * rate); ++i) {
        float close, far, piezo;
        voice.ProcessSample(close, far, piezo);
    }
    auto reference = maremba::MarembaVoiceTestAccess::Contact(voice);
    require(reference.Active(), "bend fixture missed active contact");
    const double oldCompliance = maremba::MarembaVoiceTestAccess::Compliance(voice);
    voice.Retune(cents);
    const double delta = maremba::MarembaVoiceTestAccess::Compliance(voice) - oldCompliance;
    double peak = 0.0, error = 0.0;
    for (int i = 0; i < static_cast<int>(.008 * rate); ++i) {
        const double surface = maremba::MarembaVoiceTestAccess::FreeSurface(voice);
        float close, far, piezo;
        voice.ProcessSample(close, far, piezo);
        const double force = voice.GetContactForce();
        const double expected = reference.Step(surface + delta * force);
        require(std::isfinite(close) && std::isfinite(far) && std::isfinite(piezo), "bend produced nonfinite output");
        peak = std::max(peak, std::abs(force));
        error = std::max(error, std::abs(force - expected));
    }
    require(peak > 1.0 && error < peak * 2.e-10,
            "retuned voice contact violates its new driving-point response");
}
}

int main() {
    for (double rate : {96000., 384000.}) {
        for (double compliance : {0.0, 5.e-10, 8.e-9})
            for (bool held : {false, true}) standalone(rate, compliance, held);
        for (int model : {0, 1, 2}) for (int note : {60, 96}) {
            noteOn(model, note, rate);
            for (float cents : {-1200.f, 1200.f}) bend(model, note, rate, cents);
        }
    }
    std::cout << "ContactComplianceUpdateTest PASSED: 12 state-preserving updates, 12 coupled note-ons, 24 live bends\n";
}
