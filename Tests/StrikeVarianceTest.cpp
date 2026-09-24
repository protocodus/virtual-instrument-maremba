// Control regression: audible, smoothly increasing contact/timbre variation
// from the bottom of the range through the public engine API.
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

// Frozen shipping character generator. The response curve may change between
// its endpoints, but the natural floor, maximum and draw sequence must not.
maremba::StrikeCharacter shipping(maremba::FastPRNG& prng, float variance) {
    const double amount = std::isfinite(variance) ? std::clamp(variance, 0.0f, 100.0f) * .01 : 0;
    const double upper = std::max(0., (amount - .25) / .75);
    const double expressive = upper * upper;
    maremba::StrikeCharacter s;
    s.contactTimeScale = 1 + prng.NextGaussianDouble() * (.0075 + .04 * amount + .30 * expressive);
    s.pulseSkewOffset = prng.NextGaussianDouble() * (.006 + .025 * amount + .10 * expressive);
    s.impulseScale = 1 + prng.NextGaussianDouble() * (.0035 + .01 * amount + .10 * expressive);
    s.shockScale = 1 + prng.NextGaussianDouble() * (.06 * amount + .35 * expressive);
    for (double& mode : s.modalScale) mode = 1 + prng.NextGaussianDouble() * (.004 + .035 * amount + .40 * expressive);
    return s;
}

using CharacterSpread = std::array<double, 4 + maremba::kNumPitchedModes>;
CharacterSpread characterSpread(float amount) {
    CharacterSpread squared{};
    maremba::FastPRNG before(0x2468ace1), after(0x2468ace1);
    for (int n = 0; n < 1024; ++n) {
        shipping(before, amount);
        const auto s = maremba::GenerateStrikeCharacter(after, amount);
        check(s.contactTimeScale > .65 && s.contactTimeScale < 1.35
            && std::abs(s.pulseSkewOffset) < .132 && s.impulseScale > .886 && s.impulseScale < 1.114
            && s.shockScale > .589 && s.shockScale < 1.411, "strike contact exceeded physical bounds");
        CharacterSpread offsets{s.contactTimeScale - 1, s.pulseSkewOffset,
            s.impulseScale - 1, s.shockScale - 1};
        for (int mode = 0; mode < maremba::kNumPitchedModes; ++mode) {
            check(s.modalScale[mode] > .56 && s.modalScale[mode] < 1.44, "strike partial exceeded physical bounds");
            offsets[4 + mode] = s.modalScale[mode] - 1;
        }
        for (size_t i = 0; i < squared.size(); ++i) squared[i] += offsets[i] * offsets[i];
    }
    check(before.Next() == after.Next(), "strike random draw count changed");
    for (double& value : squared) value = std::sqrt(value / 1024);
    return squared;
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
    for (float amount : {-100.f, 0.f, 100.f, 1000.f, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        maremba::FastPRNG before(0x2468ace1), after(0x2468ace1);
        for (int n = 0; n < 128; ++n)
            check(same(shipping(before, amount), maremba::GenerateStrikeCharacter(after, amount)), "shipping endpoint or invalid-input response changed");
        check(before.Next() == after.Next(), "endpoint random draw count changed");
    }
    CharacterSpread previous = characterSpread(0);
    // A dense sweep catches a dead zone or reversal without requiring an exact
    // polynomial. Measure ensemble spread, not arbitrary individual strikes.
    for (int amount = 1; amount <= 100; ++amount) {
        const auto spread = characterSpread(static_cast<float>(amount));
        for (size_t i = 0; i < spread.size(); ++i) {
            check(spread[i] > previous[i], "strike-character spread did not increase across the control range");
        }
        previous = spread;
    }
    constexpr std::array<float, 6> amounts{0, 5, 15, 25, 50, 100};
    for (int model = 0; model < 4; ++model) {
        maremba::EngineParameters p;
        p.model = model;
        p.malletHardness = .5f; p.strikePosition = .45f; p.decay = .4f;
        p.artifacts = p.detune = p.buzzAmount = p.sympathetic = p.bodyBloom = 0;
        p.farLevel = p.piezoLevel = p.preampDrive = p.compAmount = p.warmth = 0;
        p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = 0;
        maremba::MarembaEngine engine(48000);
        std::array<double, amounts.size()> timbre{};
        for (size_t step = 0; step < amounts.size(); ++step) {
            p.strikeJitter = amounts[step];
            engine.SetParameters(p); engine.Reset();
            const Hits hits = render(engine);
            engine.Reset();
            check(hits == render(engine), "reset did not replay the variance phrase exactly");
            timbre[step] = medianResidual(hits);
            const double span = levelSpan(hits);
            std::cout << "VARIANCE model=" << model << " amount=" << amounts[step]
                      << " median_timbre_residual=" << timbre[step] << " attack_level_span_db=" << span << '\n';
            if (step > 0)
                check(timbre[step] > timbre[step - 1], "phrase timbre variation did not increase across control checkpoints");
            if (amounts[step] == 100)
                check(span > 1.0 && span < 6.0, "maximum Variance attack-level range is too weak or excessive");
            auto gainOnly = hits.front();
            for (float& value : gainOnly) value *= 1.2f;
            check(residual(hits.front(), gainOnly) < 1.e-6, "timbre metric mistakes scalar gain for timbre");
        }
        check(timbre[1] > .005 && timbre[1] > 2 * timbre[0], "5% Variance lacks meaningful pitched-timbre variation");
        check(timbre[2] > .015 && timbre[2] > 4 * timbre[0] && timbre[2] > 1.5 * timbre[1],
            "15% Variance lacks a substantial increase in pitched-timbre variation");
        check(timbre.back() > .04 && timbre.back() > 8 * timbre[0], "maximum Variance lacks substantial pitched-timbre variation");
    }
    std::cout << "StrikeVarianceTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
