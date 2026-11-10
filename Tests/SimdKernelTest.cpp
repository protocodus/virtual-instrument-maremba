// The vector kernels of SimdLanes.h against the scalar loops they replace,
// bit for bit: the sympathetic receivers in pairs, the halo summed per source
// run, the Close capture's butterflies, delays and spectrum sums, and the
// Room's tap interpolation. Seeds, rates and lengths are fixed. Where the
// compiler has no vector extensions both sides are the scalar loop.
#include "CaptureScene.h"
#include "SympatheticMesh.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace maremba {
struct SympatheticMeshTestAccess {
    static void AdvanceScalar(SympatheticMesh& mesh, float amount, float* pressure) {
        mesh.DispatchAdvance<true, false>(amount, pressure);
    }
};
struct CaptureSceneTestAccess {
    static bool Active(const CaptureScene& scene, int id) { return scene.mSource[id].active; }
};
struct RoomCaptureTestAccess {
    template<bool Lanes>
    static void InterpolateTap(const float* h, float fraction, float gain, int frames, float* out) {
        RoomCapture::InterpolateTap<Lanes>(h, fraction, gain, frames, out);
    }
};
}

namespace {
using namespace maremba;
std::uint64_t checks = 0;
void require(bool okay, const char* message) {
    ++checks;
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool same(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

struct Random {
    std::uint32_t state;
    std::uint32_t Next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    float Unit() { return static_cast<float>(Next() & 0xffffu) / 32768.0f - 1.0f; }
    int Below(int n) { return static_cast<int>(Next() % static_cast<std::uint32_t>(n)); }
};

void receivers(EMarimbaModel model, int tuningScale, const char* name) {
    SympatheticMesh lanes, scalar;
    lanes.Configure(48000.0, 7.0, model, tuningScale);
    scalar.Configure(48000.0, 7.0, model, tuningScale);
    Random random{0x51d0u + static_cast<std::uint32_t>(model) * 97u};
    std::array<float, SympatheticMesh::kNumResonators> a{}, b{};
    int ringing = 0;
    for (int frame = 0; frame < 24000; ++frame) {
        lanes.BeginSample(); scalar.BeginSample();
        // Sounding keys leave and rejoin the receivers, in and across pairs.
        const int voices = frame % 4000 < 3000 ? random.Below(4) : 0;
        for (int voice = 0; voice < voices; ++voice) {
            const int note = 36 + random.Below(61);
            const double drive = frame % 480 < 40 ? random.Unit() : 1.0e-3 * random.Unit();
            lanes.AccumulateVoice(note, drive); scalar.AccumulateVoice(note, drive);
        }
        lanes.AdvanceAndRead(.3f, a.data());
        SympatheticMeshTestAccess::AdvanceScalar(scalar, .3f, b.data());
        for (int i = 0; i < lanes.ReceiverCount(); ++i) {
            require(same(a[i], b[i]), name);
            ringing += a[i] != 0.0f;
        }
    }
    require(ringing > 100000, "the receivers rang");
}

void haloRuns(int factor) {
    auto runs = std::make_unique<CaptureScene>();
    auto calls = std::make_unique<CaptureScene>();
    runs->Allocate(48000.0); calls->Allocate(48000.0);
    Random random{0xa11ceu + static_cast<std::uint32_t>(factor)};
    // An octave's source, which a struck F shares, and one that takes a lone
    // receiver or seven: whole vectors of products, a scalar tail, or both.
    const std::array<int, 2> sources{{CaptureSourceIndex(EMarimbaModel::ImperialRosewood, 65),
                                      CaptureSourceIndex(EMarimbaModel::ImperialRosewood, 96)}};
    const auto both = [&](auto&& act) { act(*runs); act(*calls); };
    int sounding = 0;
    for (int frame = 0; frame < 6000; ++frame) {
        both([&](CaptureScene& scene) { scene.BeginFrame(factor); });
        const int phase = frame % 1500;
        for (int s = 0; s < factor; ++s) {
            // A voice on the shared source: silent, equal ears, then unequal.
            if (phase >= 300 && phase < 900) {
                const float left = .2f * random.Unit(), right = phase < 600 ? left : .2f * random.Unit();
                both([&](CaptureScene& scene) { scene.Add(sources[0], s, left, right, .4f * left); });
            }
            for (int source : sources) {
                std::array<float, 12> pressure{};
                const int count = source == sources[0] ? 12 : frame % 2 == 0 ? 1 : 7;
                // All zero (the source must stay asleep), then zeros of both
                // signs and vanishing terms among ordinary ones.
                if (phase >= 100 && phase < 1200) for (int k = 0; k < count; ++k) {
                    const int kind = random.Below(8);
                    pressure[k] = kind == 0 ? 0.0f : kind == 1 ? -0.0f : kind == 2 ? 2.0e-45f
                                : kind == 3 ? -1.0e-38f * random.Unit() : .05f * random.Unit();
                }
                runs->AddCompactPressures(source, s, pressure.data(), count, .35f);
                for (int k = 0; k < count; ++k) calls->AddCompactPressure(source, s, .35f * pressure[k]);
                require(CaptureSceneTestAccess::Active(*runs, source) == CaptureSceneTestAccess::Active(*calls, source),
                        "a halo run wakes its source when the receiver calls do");
            }
        }
        require(runs->HasInvalidInput() == calls->HasInvalidInput(), "halo run validity");
        float a[4], b[4];
        runs->Process(a[0], a[1], a[2], a[3]);
        calls->Process(b[0], b[1], b[2], b[3]);
        for (int i = 0; i < 4; ++i) {
            require(same(a[i], b[i]), "halo run capture");
            sounding += a[i] != 0.0f;
        }
    }
    require(sounding > 12000, "the halo runs reached the microphones");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        runs->Reset();
        runs->BeginFrame(factor);
        const std::array<float, 3> pressure{{.1f, invalid, -.2f}};
        runs->AddCompactPressures(sources[0], factor - 1, pressure.data(), 3, .35f);
        require(runs->HasInvalidInput(), "a non-finite halo term is reported");
    }
}

void closeCapture(double rate) {
    constexpr int kSources = 6;
    const std::array<int, kSources> ids{{CaptureSourceIndex(EMarimbaModel::ImperialRosewood, 36),
        CaptureSourceIndex(EMarimbaModel::ImperialRosewood, 65), CaptureSourceIndex(EMarimbaModel::BalafonAncestral, 77),
        CaptureSourceIndex(EMarimbaModel::KalimbaArtisan, 72), kCaptureFrameSource, kCaptureHoleSource}};
    auto lanes = std::make_unique<CloseCaptureKernel<true>>();
    auto scalar = std::make_unique<CloseCaptureKernel<false>>();
    lanes->Allocate(192000.0, kCaptureSourceCount); scalar->Allocate(192000.0, kCaptureSourceCount);
    lanes->SetSampleRate(rate); scalar->SetSampleRate(rate);
    for (int id : ids) {
        const bool a = lanes->AddSource(id, CaptureSourcePosition(id), kCloseHrtfSourceProfile[id]);
        const bool b = scalar->AddSource(id, CaptureSourcePosition(id), kCloseHrtfSourceProfile[id]);
        require(a && b, "Close source fits");
    }
    Random random{0xc105eu + static_cast<std::uint32_t>(rate)};
    std::vector<float> left(kCaptureSourceCount), right(kCaptureSourceCount);
    const int frames = static_cast<int>(rate * .35);
    int heard = 0;
    for (int frame = 0; frame < frames; ++frame) {
        // Bursts with silences long enough for sources to retire and return.
        const bool sounding = frame % (frames / 3) < frames / 9;
        for (int n = 0; n < kSources; ++n) {
            const int id = ids[static_cast<std::size_t>(n)];
            const float l = sounding && (frame / 97 + n) % 3 != 0 ? .3f * random.Unit() : 0.0f;
            left[static_cast<std::size_t>(id)] = l;
            right[static_cast<std::size_t>(id)] = n % 2 == 0 ? l : (l != 0.0f ? .3f * random.Unit() : 0.0f);
        }
        float a[2], b[2];
        lanes->Process(left.data(), right.data(), kCaptureSourceCount, a[0], a[1]);
        scalar->Process(left.data(), right.data(), kCaptureSourceCount, b[0], b[1]);
        require(same(a[0], b[0]) && same(a[1], b[1]), "Close capture lanes");
        heard += a[0] != 0.0f && a[1] != 0.0f;
    }
    require(heard > frames / 4, "the Close capture sounded");
    require(lanes->PartitionFrames() == scalar->PartitionFrames(), "Close partition");
}

void roomTaps() {
    Random random{0x7a95u};
    std::array<float, 80> history, lanes, scalar;
    for (int round = 0; round < 4000; ++round) {
        for (float& value : history) value = random.Unit();
        for (std::size_t i = 0; i < lanes.size(); ++i) lanes[i] = scalar[i] = random.Unit();
        const int frames = random.Below(65);
        const float fraction = .5f + .5f * random.Unit(), gain = random.Unit();
        RoomCaptureTestAccess::InterpolateTap<true>(history.data() + 1, fraction, gain, frames, lanes.data() + 3);
        RoomCaptureTestAccess::InterpolateTap<false>(history.data() + 1, fraction, gain, frames, scalar.data() + 3);
        for (std::size_t i = 0; i < lanes.size(); ++i) require(same(lanes[i], scalar[i]), "Room tap lanes");
    }
}
}

int main() {
    receivers(EMarimbaModel::ImperialRosewood, 0, "bar receivers in pairs");
    receivers(EMarimbaModel::BalafonAncestral, 1, "degree-key receivers in pairs");
    receivers(EMarimbaModel::KalimbaArtisan, 0, "tine receivers in pairs");
    for (int factor : {1, 4}) haloRuns(factor);
    // 8 kHz has blocks too short for a vector; the others have 128- to 512-point transforms.
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) closeCapture(rate);
    roomTaps();
    std::printf("SimdKernel: %llu bit-exact checks, vector kernels %s\n",
                static_cast<unsigned long long>(checks), MAREMBA_SIMD ? "on" : "off (scalar build)");
    return 0;
}
