// Control regression: preserve the natural low range, then add audible
// contact/timbre changes at high Variance through the public engine API.
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}

// Frozen pre-change character generator. Keeping this separate from the
// production curve catches low-range drift and changes to the draw sequence.
maremba::StrikeCharacter legacy(maremba::FastPRNG& prng, float variance) {
    const double amount = std::isfinite(variance) ? std::clamp(variance, 0.0f, 100.0f) * .01 : 0;
    maremba::StrikeCharacter s;
    s.contactTimeScale = 1 + prng.NextGaussianDouble() * (.0075 + .04 * amount);
    s.pulseSkewOffset = prng.NextGaussianDouble() * (.006 + .025 * amount);
    s.impulseScale = 1 + prng.NextGaussianDouble() * (.0035 + .01 * amount);
    s.shockScale = 1 + prng.NextGaussianDouble() * (.06 * amount);
    for (double& mode : s.modalScale) mode = 1 + prng.NextGaussianDouble() * (.004 + .035 * amount);
    return s;
}

bool same(const maremba::StrikeCharacter& a, const maremba::StrikeCharacter& b) {
    return a.contactTimeScale == b.contactTimeScale && a.pulseSkewOffset == b.pulseSkewOffset
        && a.impulseScale == b.impulseScale && a.shockScale == b.shockScale
        && std::equal(std::begin(a.modalScale), std::end(a.modalScale), std::begin(b.modalScale));
}

using Hits = std::vector<std::vector<float>>;
Hits render(maremba::MarembaEngine& engine) {
    Hits hits;
    for (int hit = 0; hit < 12; ++hit) {
        check(engine.IsSilent(), "public-engine strike did not start from silence");
        engine.NoteOn(60, 90.f / 127);
        std::vector<float> clip;
        std::array<float, 64> left{}, right{}, closeLeft{}, closeRight{};
        for (int batch = 0; batch < 150; ++batch) { // first 200 ms at 48 kHz
            engine.RenderBatch(left.data(), right.data(), closeLeft.data(), closeRight.data(), nullptr, nullptr, nullptr, 64);
            for (size_t i = 0; i < left.size(); ++i) {
                check(std::isfinite(left[i]) && std::isfinite(right[i]), "nonfinite strike output");
                clip.push_back(closeLeft[i] + closeRight[i]);
            }
        }
        hits.push_back(std::move(clip));
        engine.DampAll();
        for (int batch = 0; batch < 48000 * 3 / 64 && !engine.IsSilent(); ++batch)
            engine.RenderBatch(left.data(), right.data(), nullptr, nullptr, nullptr, nullptr, nullptr, 64);
        check(engine.IsSilent(), "strike failed to damp to silence");
    }
    return hits;
}

double energy(const std::vector<float>& samples, size_t begin, size_t end) {
    double value = 0;
    for (size_t i = begin; i < end; ++i) value += static_cast<double>(samples[i]) * samples[i];
    return value;
}

// Remove the best scalar gain before comparing late pitched waveforms. A
// changing volume or attack-noise burst alone cannot pass the timbre gate.
double residual(const std::vector<float>& a, const std::vector<float>& b) {
    double aa = 0, ab = 0, bb = 0;
    for (size_t i = 48000 * .04; i < 48000 * .16; ++i) {
        aa += static_cast<double>(a[i]) * a[i];
        ab += static_cast<double>(a[i]) * b[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return aa > 0 && bb > 0 ? std::sqrt(std::max(0., 1 - ab * ab / (aa * bb))) : 0;
}

double medianResidual(const Hits& hits) {
    std::vector<double> values;
    for (size_t h = 1; h < hits.size(); ++h) values.push_back(residual(hits[h - 1], hits[h]));
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double levelSpan(const Hits& hits) {
    double lo = 1.e100, hi = -1.e100;
    for (const auto& hit : hits) {
        const double db = 10 * std::log10(energy(hit, 0, 48000 * .04));
        lo = std::min(lo, db); hi = std::max(hi, db);
    }
    return hi - lo;
}
} // namespace

int main() {
    for (float amount : {-100.f, 0.f, 15.f, 25.f, std::numeric_limits<float>::quiet_NaN()}) {
        maremba::FastPRNG before(0x2468ace1), after(0x2468ace1);
        for (int n = 0; n < 128; ++n)
            check(same(legacy(before, amount), maremba::GenerateStrikeCharacter(after, amount)), "legacy low-range response changed");
        check(before.Next() == after.Next(), "low-range random draw count changed");
    }
    for (float amount : {25.01f, 50.f, 100.f, 1000.f}) {
        maremba::FastPRNG before(3), after(3);
        for (int n = 0; n < 128; ++n) {
            legacy(before, amount);
            const auto s = maremba::GenerateStrikeCharacter(after, amount);
            check(s.contactTimeScale > .65 && s.contactTimeScale < 1.35
                && std::abs(s.pulseSkewOffset) < .132 && s.impulseScale > .886 && s.impulseScale < 1.114
                && s.shockScale > .589 && s.shockScale < 1.411, "upper-range contact exceeded physical bounds");
            for (double mode : s.modalScale) check(mode > .56 && mode < 1.44, "upper-range partial exceeded physical bounds");
        }
        check(before.Next() == after.Next(), "upper-range random draw count changed");
    }
    for (int model = 0; model < 4; ++model) {
        maremba::EngineParameters p;
        p.model = model;
        p.malletHardness = .5f; p.strikePosition = .45f; p.decay = .4f;
        p.artifacts = p.detune = p.buzzAmount = p.sympathetic = p.bodyBloom = 0;
        p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
        p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
        p.strikeJitter = 0;
        maremba::MarembaEngine engine(48000);
        engine.SetParameters(p);
        const Hits zero = render(engine);
        p.strikeJitter = 100;
        engine.SetParameters(p); engine.Reset();
        const Hits maximum = render(engine);
        engine.Reset();
        check(maximum == render(engine), "reset did not replay the high-variance phrase exactly");
        const double lowResidual = medianResidual(zero), highResidual = medianResidual(maximum);
        const double span = levelSpan(maximum);
        std::cout << "VARIANCE model=" << model << " zero_timbre_residual=" << lowResidual
                  << " max_timbre_residual=" << highResidual << " max_attack_level_span_db=" << span << '\n';
        check(highResidual > .04 && highResidual > 8 * lowResidual, "maximum Variance lacks substantial pitched-timbre variation");
        check(span > 1.0 && span < 6.0, "maximum Variance attack-level range is too weak or excessive");
        auto gainOnly = maximum.front();
        for (float& value : gainOnly) value *= 1.2f;
        check(residual(maximum.front(), gainOnly) < 1.e-6, "timbre metric mistakes scalar gain for timbre");
    }
    std::cout << "StrikeVarianceTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
