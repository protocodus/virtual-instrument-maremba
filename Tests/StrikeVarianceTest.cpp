// Dual control regression: retain calibrated physical-strike thresholds in the
// legacy anechoic reference, and verify monotonic non-gain timbre variation in
// the complete measured binaural Close capture. Both observe the same ongoing
// public-engine 48-hit sequence and retain full physical/room lifecycle gates.
#include "MarembaEngine.h"
#include "MicMixer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    // Clone this actual ongoing phrase's engine-created strike. A fresh engine
    // per hit would erase the 48-hit PRNG sequence and make a false oracle.
    static std::vector<float> AnechoicBar(const MarembaEngine& engine,int frames) {
        const auto& p=engine.mParams;
        if(p.model<0||p.model>2||p.bodyBloom!=0||p.sympathetic!=0||p.velocityToBloom!=0
            ||p.velocityToSympathetic!=0||p.modWheel!=0)std::abort();
        const MarembaVoice* struck=nullptr;
        for(const auto& voice:engine.mVoices)if(voice.IsActive()){if(struck)std::abort();struck=&voice;}
        if(!struck)std::abort();
        MarembaVoice voice=*struck;
        const int factor=engine.mActiveFactor;
        int control=engine.mControlCounter;
        MonoOversampler left,right;
        MicMixer reference;
        std::array<float,MarembaEngine::kMaxOversample> subLeft{},subRight{};
        std::vector<float> output(static_cast<size_t>(frames),0.f);
        const float gain=MicMixer::kMasterMixScale*.06f*engine.mVolumeGain;
        constexpr int outputDelay=96; //2ms at this fixture's48kHz
        for(int frame=0;frame<frames;++frame){
            for(int sub=0;sub<factor;++sub){
                float vc=0,vf=0,vp=0,cl=0,cr=0,fl=0,fr=0,piezo=0;
                if(voice.IsActive()&&voice.ProcessSample(vc,vf,vp))
                    reference.MixVoiceSample(60,false,vc,vf,vp,cl,cr,fl,fr,piezo);
                subLeft[sub]=cl;subRight[sub]=cr;
            }
            const float l=left.DownsampleFrame(subLeft.data(),factor)*gain;
            const float r=right.DownsampleFrame(subRight.data(),factor)*gain;
            if(frame+outputDelay<frames)output[static_cast<size_t>(frame+outputDelay)]=l+r;
            if(++control>=MarembaEngine::kControlInterval){control=0;if(voice.IsActive())voice.UpdateEnvelope();left.FlushTiny();right.FlushTiny();}
        }
        return output;
    }
};
}

namespace {
int failures = 0;
int drainedHits = 0, longestVoiceDampFrames = 0, longestEngineDrainFrames = 0;
std::uint64_t totalEngineDrainFrames = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}

// Frozen shipping character generator (1.4.0b1's physical stroke: head
// stiffness, stick speed, landing point, lateral offset, head loss). The
// response curve may change between its endpoints, but the natural floor,
// maximum and draw sequence must not.
maremba::StrikeCharacter shipping(maremba::FastPRNG& prng, float variance) {
    const double amount = std::isfinite(variance) ? std::clamp(variance, 0.0f, 100.0f) * .01 : 0;
    maremba::StrikeCharacter s;
    s.stiffnessScale = 1 + prng.NextGaussianDouble() * (.03 + .35 * amount);
    s.velocityScale = 1 + prng.NextGaussianDouble() * (.004 + .08 * amount);
    s.positionOffset = prng.NextGaussianDouble() * (.0012 + .07 * amount);
    s.lateralOffset = prng.NextGaussianDouble() * (.25 + .30 * amount);
    s.hysteresisScale = 1 + prng.NextGaussianDouble() * (.02 + .15 * amount);
    return s;
}

using CharacterSpread = std::array<double, 5>;
CharacterSpread characterSpread(float amount) {
    CharacterSpread squared{};
    maremba::FastPRNG before(0x2468ace1), after(0x2468ace1);
    for (int n = 0; n < 1024; ++n) {
        shipping(before, amount);
        const auto s = maremba::GenerateStrikeCharacter(after, amount);
        // Each draw is the mean of three uniforms, so the maximum spreads bound it.
        check(s.stiffnessScale > .619 && s.stiffnessScale < 1.381 && s.velocityScale > .915 && s.velocityScale < 1.085
            && std::abs(s.positionOffset) < .0713 && std::abs(s.lateralOffset) < .551
            && s.hysteresisScale > .829 && s.hysteresisScale < 1.171, "strike contact exceeded physical bounds");
        const CharacterSpread offsets{s.stiffnessScale - 1, s.velocityScale - 1, s.positionOffset,
                                      s.lateralOffset, s.hysteresisScale - 1};
        for (size_t i = 0; i < squared.size(); ++i) squared[i] += offsets[i] * offsets[i];
    }
    check(before.Next() == after.Next(), "strike random draw count changed");
    for (double& value : squared) value = std::sqrt(value / 1024);
    return squared;
}

bool same(const maremba::StrikeCharacter& a, const maremba::StrikeCharacter& b) {
    return a.stiffnessScale == b.stiffnessScale && a.velocityScale == b.velocityScale
        && a.positionOffset == b.positionOffset && a.lateralOffset == b.lateralOffset
        && a.hysteresisScale == b.hysteresisScale;
}

using Hits = std::vector<std::vector<float>>;
struct Phrase { Hits physical,closeStereo; };
Phrase render(maremba::MarembaEngine& engine) {
    Phrase hits;
    // Physical contact is nonlinear: a twelve-hit median can move down when
    // its physical spread increases. Use forty-eight uninterrupted strikes
    // so low-range timbre gates measure the ensemble rather than one short
    // phrase, while retaining the same seeds, reset replay and thresholds.
    for (int hit = 0; hit < 48; ++hit) {
        check(engine.IsSilent(), "public-engine strike did not start from silence");
        engine.NoteOn(60, 90.f / 127);
        // Kalimba's tone includes its shared board/hole, so retain its
        // existing complete L+R oracle rather than pretending one raw tine
        // represents the instrument. Bars have all shared layers disabled.
        const bool bar=engine.GetParameters().model<3;
        std::vector<float> physical=bar?maremba::MarembaEngineTestAccess::AnechoicBar(engine,9600):std::vector<float>{};
        std::vector<float> clip;
        clip.reserve(19200);if(!bar)physical.reserve(9600);
        std::array<float, 64> left{}, right{}, closeLeft{}, closeRight{};
        for (int batch = 0; batch < 150; ++batch) { // first 200 ms at 48 kHz
            engine.RenderBatch(left.data(), right.data(), closeLeft.data(), closeRight.data(), nullptr, nullptr, nullptr, 64);
            for (size_t i = 0; i < left.size(); ++i) {
                check(std::isfinite(left[i]) && std::isfinite(right[i]), "nonfinite strike output");
                check(std::isfinite(closeLeft[i])&&std::isfinite(closeRight[i]),"nonfinite complete Close output");
                clip.push_back(closeLeft[i]);clip.push_back(closeRight[i]);
                if(!bar)physical.push_back(closeLeft[i]+closeRight[i]);
            }
        }
        hits.physical.push_back(std::move(physical));
        hits.closeStereo.push_back(std::move(clip));
        engine.DampAll();
        // DampAll stops held/rolling keys and hand-damps every voice,
        // independently of the Note Off mode. Preserve that three-second
        // physical deadline, then let the continuously running Room response
        // finish even when its mix coefficient is zero. Do not Reset here:
        // successive hits must retain their natural random draw sequence.
        constexpr int voiceDeadlineFrames = 48000 * 3;
        constexpr int engineDeadlineFrames = 48000 * 12;
        int drainFrames = 0;
        int voiceDampFrames = engine.GetActiveVoiceCount() == 0 ? 0 : -1;
        for (; drainFrames < engineDeadlineFrames && !engine.IsSilent(); drainFrames += 64) {
            engine.RenderBatch(left.data(), right.data(), nullptr, nullptr, nullptr, nullptr, nullptr, 64);
            if (voiceDampFrames < 0 && engine.GetActiveVoiceCount() == 0)
                voiceDampFrames = drainFrames + 64;
        }
        check(voiceDampFrames >= 0 && voiceDampFrames <= voiceDeadlineFrames,
              "strike voices failed to hand-damp within three seconds");
        check(engine.IsSilent(), "strike and room tail failed to reach engine silence within twelve seconds");
        ++drainedHits;
        longestVoiceDampFrames = std::max(longestVoiceDampFrames, voiceDampFrames);
        longestEngineDrainFrames = std::max(longestEngineDrainFrames, drainFrames);
        totalEngineDrainFrames += drainFrames;
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
double residual(const std::vector<float>& a, const std::vector<float>& b,int channels=1) {
    double aa = 0, ab = 0, bb = 0;
    for (size_t i = static_cast<size_t>(48000 * .04 * channels); i < static_cast<size_t>(48000 * .16 * channels); ++i) {
        aa += static_cast<double>(a[i]) * a[i];
        ab += static_cast<double>(a[i]) * b[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return aa > 0 && bb > 0 ? std::sqrt(std::max(0., 1 - ab * ab / (aa * bb))) : 0;
}

double medianResidual(const Hits& hits,int channels=1) {
    std::vector<double> values;
    for (size_t h = 1; h < hits.size(); ++h) values.push_back(residual(hits[h - 1], hits[h],channels));
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double levelSpan(const Hits& hits,int channels=1) {
    double lo = 1.e100, hi = -1.e100;
    for (const auto& hit : hits) {
        const double db = 10 * std::log10(energy(hit, 0, static_cast<size_t>(48000 * .04 * channels)));
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
        std::array<double, amounts.size()> timbre{},capturedTimbre{};
        for (size_t step = 0; step < amounts.size(); ++step) {
            p.strikeJitter = amounts[step];
            engine.SetParameters(p); engine.Reset();
            const Phrase hits = render(engine);
            engine.Reset();
            const Phrase replay=render(engine);
            check(hits.physical==replay.physical&&hits.closeStereo==replay.closeStereo,"reset did not replay both variance observations exactly");
            timbre[step] = medianResidual(hits.physical);
            capturedTimbre[step]=medianResidual(hits.closeStereo,2);
            const double span = levelSpan(hits.physical);
            check(capturedTimbre[step]>1.e-6,"complete Close loses non-gain strike variation");
            if(step>0)check(capturedTimbre[step]>capturedTimbre[step-1],"complete stereo Close variation does not grow across checkpoints");
            std::cout << "VARIANCE model=" << model << " amount=" << amounts[step]
                      << " physical_reference=" << (model<3?"anechoic_bar":"complete_kalimba")
                      << " physical_median_timbre_residual=" << timbre[step] << " attack_level_span_db=" << span
                      << " close_stereo_residual=" << capturedTimbre[step] << " close_attack_span_db=" << levelSpan(hits.closeStereo,2) << std::endl;
            if (step > 0)
                check(timbre[step] > timbre[step - 1], "phrase timbre variation did not increase across control checkpoints");
            if (amounts[step] == 100)
                check(span > 1.0 && span < 6.0, "maximum Variance attack-level range is too weak or excessive");
            auto gainOnly = hits.physical.front();
            for (float& value : gainOnly) value *= 1.2f;
            check(residual(hits.physical.front(), gainOnly) < 1.e-6, "physical timbre metric mistakes scalar gain for timbre");
            auto stereoGainOnly=hits.closeStereo.front();
            for(float& value:stereoGainOnly)value*=1.2f;
            check(residual(hits.closeStereo.front(),stereoGainOnly,2)<1.e-6,"stereo timbre metric mistakes scalar gain for timbre");
        }
        // Rosewood's tube carries its ring's fundamental (the bloom), a scalar
        // this metric removes, so the same physical stroke-to-stroke change
        // reaches its pitched window about three quarters as strongly.
        const double reach = model == 0 ? .75 : 1.;
        check(timbre[1] > .005 * reach && timbre[1] > 2 * reach * timbre[0], "5% Variance lacks meaningful pitched-timbre variation");
        // A snappy thumb release is less sensitive to the head's stiffness
        // scatter, so the kalimba's step from 5% to 15% is a little smaller.
        check(timbre[2] > .015 * reach && timbre[2] > 4 * reach * timbre[0]
              && timbre[2] > (model == 3 ? 1.4 : 1.5) * timbre[1],
            "15% Variance lacks a substantial increase in pitched-timbre variation");
        check(timbre.back() > .04 * reach && timbre.back() > 8 * timbre[0], "maximum Variance lacks substantial pitched-timbre variation");
    }
    std::cout << "StrikeVarianceTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures
              << " drained_hits=" << drainedHits
              << " longest_voice_damp_seconds=" << longestVoiceDampFrames / 48000.0
              << " longest_engine_drain_seconds=" << longestEngineDrainFrames / 48000.0
              << " total_engine_drain_seconds=" << totalEngineDrainFrames / 48000.0 << '\n';
    return failures ? 1 : 0;
}
