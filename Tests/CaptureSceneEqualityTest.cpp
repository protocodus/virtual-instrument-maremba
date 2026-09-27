// Exact equivalence of lazy equal-ear decimation to three independent filters.
// MAREMBA_CAPTURE_BASELINE lets this same stimulus dump run against the frozen
// pre-optimization header for an independent whole-capture comparison.
#include "CaptureScene.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace maremba {
struct CaptureSceneTestAccess {
    static void ForceIndependent(CaptureScene& scene) {
#ifndef MAREMBA_CAPTURE_BASELINE
        // This reference always processes all three independent decimators;
        // no right-channel state or output is copied from the left channel.
        for (auto& source : scene.mSource) source.equalEars = false;
#else
        (void)scene;
#endif
    }
    static float Decimated(const CaptureScene& scene, int id, int lane) {
        return scene.mInput[lane][id];
    }
    static bool Active(const CaptureScene& scene, int id) { return scene.mSource[id].active; }
    static bool SourceNormal(const CaptureScene& scene, int id) {
        for (const auto& decimator : scene.mSource[id].decimator)
            if (!decimator.HasNoSubnormalState()) return false;
        return true;
    }
};
}
namespace {
using namespace maremba;
std::uint64_t checks = 0;
int cases = 0, currentFactor = 0, currentPosition = 0, currentFrame = 0;
double currentRate = 0;
constexpr std::array<int, 4> ids{{60, 61, kCaptureFrameSource, kCaptureHoleSource}};
void require(bool okay, const char* message) {
    ++checks;
    if (!okay) {
        std::fprintf(stderr, "FAIL rate=%.0f factor=%d first_asymmetry=%d frame=%d: %s\n",
                     currentRate, currentFactor, currentPosition, currentFrame, message);
        std::exit(1);
    }
}
bool same(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
void add(CaptureScene& a, CaptureScene& b, int id, int sub, float left, float right, float room) {
    a.Add(id, sub, left, right, room); b.Add(id, sub, left, right, room);
    require(a.HasInvalidInput() == b.HasInvalidInput(), "invalid-source flags differ");
}
void invalidInputs(CaptureScene& lazy, CaptureScene& eager, int factor) {
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    for (float value : invalid) for (int lane = 0; lane < 3; ++lane) {
        lazy.Reset(); eager.Reset(); CaptureSceneTestAccess::ForceIndependent(eager);
        lazy.BeginFrame(factor); eager.BeginFrame(factor);
        add(lazy, eager, ids[0], 0, .125f, .125f, .0625f);
        std::array<float, 3> input{{.25f, .25f, .125f}}; input[lane] = value;
        add(lazy, eager, ids[0], factor - 1, input[0], input[1], input[2]);
        require(lazy.HasInvalidInput(), "nonfinite input was concealed");
        lazy.BeginFrame(factor); eager.BeginFrame(factor);
        require(!lazy.HasInvalidInput(), "new frame retains invalid flag");
    }
    for (int lane = 0; lane < 4; ++lane) {
        lazy.Reset(); eager.Reset(); CaptureSceneTestAccess::ForceIndependent(eager);
        lazy.BeginFrame(factor); eager.BeginFrame(factor);
        std::array<float, 3> huge{};
        if (lane == 3) huge[0] = huge[1] = std::numeric_limits<float>::max();
        else huge[lane] = std::numeric_limits<float>::max();
        for (int i = 0; i < 2; ++i) add(lazy, eager, ids[0], 0, huge[0], huge[1], huge[2]);
        require(lazy.HasInvalidInput(), "finite contribution sum overflow was concealed");
    }
    lazy.Reset(); eager.Reset();
    require(!lazy.HasInvalidInput() && !eager.HasInvalidInput(), "reset retains invalid flag");
}
}
int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) return 2;
    if (argc == 3 && std::strcmp(argv[1], "--dump") != 0) return 2;
    FILE* dump = argc == 3 ? std::fopen(argv[2], "wb") : nullptr;
    if (argc == 3 && !dump) return 3;
    for (double rate : {8000., 11025., 16000., 22050., 24000., 32000., 44100.,
                        48000., 88200., 96000., 176400., 192000., 384000.}) {
        currentRate = rate;
        auto lazy = std::make_unique<CaptureScene>(), eager = std::make_unique<CaptureScene>();
        lazy->Allocate(rate); eager->Allocate(rate);
        for (int factor : {2, 4, 8}) {
            currentFactor = factor;
            for (int first = 0; first < factor; ++first) {
                currentPosition = first;
                lazy->SetSampleRate(rate); eager->SetSampleRate(rate);
                lazy->Reset(); eager->Reset();
                lazy->SetRoomType(first % 3); eager->SetRoomType(first % 3);
                const int frames = 800 + static_cast<int>(std::ceil(.012 * rate));
                double leftEnergy = 0, rightEnergy = 0;
                for (int frame = 0; frame < frames; ++frame) {
                    currentFrame = frame;
                    if (frame == 460) { lazy->Reset(false); eager->Reset(false); }
                    if (frame == 470) { lazy->Reset(); eager->Reset(); }
                    CaptureSceneTestAccess::ForceIndependent(*eager);
                    lazy->BeginFrame(factor); eager->BeginFrame(factor);
                    if (frame < 40 || (frame >= 400 && frame < 440)) {
                        // Reverse source/subsample order puts prior equal-ear
                        // contributions on both sides of the first asymmetric
                        // sample, including another contribution to that slot.
                        for (int source = static_cast<int>(ids.size()) - 1; source >= 0; --source)
                        for (int sub = factor - 1; sub >= 0; --sub) {
                            const float value = .03125f * static_cast<float>(1 + (frame + sub + source) % 7);
                            add(*lazy, *eager, ids[source], sub, value, value, -.375f * value);
                            lazy->AddCompactPressure(ids[source], sub, -.25f * value);
                            eager->AddCompactPressure(ids[source], sub, -.25f * value);
                        }
                        if (frame == 32 || frame == 432) {
                            for (int id : ids) {
                                add(*lazy, *eager, id, first, -.125f, .1875f, -.0625f);
                                add(*lazy, *eager, id, first, .015625f, .015625f, 0);
                                add(*lazy, *eager, id, first, .25f, -.03125f, .0625f);
                            }
                        }
                    } else if (frame >= 56 && frame < 72) {
                        // Muted ears leave their filter histories intact while
                        // the same emitter still contributes to the room.
                        for (int sub = 0; sub < factor; ++sub)
                            add(*lazy, *eager, ids[0], sub, 0, 0, .0625f);
                    } else if (frame >= 480 && frame < 512) {
                        for (int sub = 0; sub < factor; ++sub) {
                            const float value = .125f * std::sin(.127f * static_cast<float>(frame * factor + sub));
                            // Alternate cases prove independence in both directions.
                            add(*lazy, *eager, ids[0], sub, first % 2 ? 0 : value,
                                first % 2 ? value : 0, 0);
                        }
                    }
                    require(!lazy->HasInvalidInput(), "finite stimulus marked invalid");
                    std::array<float, 4> a{}, b{};
                    lazy->Process(a[0], a[1], a[2], a[3]);
                    eager->Process(b[0], b[1], b[2], b[3]);
                    for (int channel = 0; channel < 4; ++channel) {
                        require(std::isfinite(a[channel]) && same(a[channel], b[channel]), "eager/lazy delivered samples differ");
                    }
                    for (int id : ids) for (int lane = 0; lane < 3; ++lane)
                        require(same(CaptureSceneTestAccess::Decimated(*lazy, id, lane),
                                     CaptureSceneTestAccess::Decimated(*eager, id, lane)), "decimated source lanes differ");
                    if (frame == 399 || frame == 799)
                        for (int id : ids) require(!CaptureSceneTestAccess::Active(*lazy, id), "emitter did not retire after256silent frames");
                    if (frame >= 470) {
                        require(a[first % 2 ? 0 : 1] == 0.0f, "silent ear is not exactly zero");
                        require(a[2] == 0.0f && a[3] == 0.0f, "zero room feed is not exactly zero after reset");
                        leftEnergy += static_cast<double>(a[0]) * a[0];
                        rightEnergy += static_cast<double>(a[1]) * a[1];
                    }
                    if ((frame + 1) % 16 == 0) {
                        lazy->FlushTiny(); eager->FlushTiny();
                        for (int id : ids)
                            require(CaptureSceneTestAccess::SourceNormal(*lazy, id) && CaptureSceneTestAccess::SourceNormal(*eager, id), "flushed source decimator contains subnormals");
                    }
                    if (dump) require(std::fwrite(a.data(), sizeof(float), a.size(), dump) == a.size(), "cannot write comparison stream");
                }
                require(first % 2 ? rightEnergy > 1e-6 && leftEnergy == 0 : leftEnergy > 1e-6 && rightEnergy == 0,
                        "one-ear isolation fixture did not reach the microphone");
                lazy->FlushTiny(); eager->FlushTiny();
                require(lazy->HasNoSubnormalState() && eager->HasNoSubnormalState(), "capture retains subnormal state after its full lifecycle");
                ++cases;
            }
            invalidInputs(*lazy, *eager, factor);
            // Rate changes reset and reconfigure both histories. Equal-ear
            // optimization must not retain right-ear state from a prior rate.
            lazy->SetSampleRate(std::max(8000., rate * .5)); eager->SetSampleRate(std::max(8000., rate * .5));
            CaptureSceneTestAccess::ForceIndependent(*eager);
            lazy->BeginFrame(factor); eager->BeginFrame(factor);
            add(*lazy, *eager, ids[0], factor - 1, .25f, -.125f, .0625f);
            std::array<float, 4> a{}, b{};
            lazy->Process(a[0], a[1], a[2], a[3]); eager->Process(b[0], b[1], b[2], b[3]);
            for (int lane = 0; lane < 3; ++lane)
                require(same(CaptureSceneTestAccess::Decimated(*lazy, ids[0], lane), CaptureSceneTestAccess::Decimated(*eager, ids[0], lane)), "rate-change source lanes differ");
        }
    }
    if (dump && std::fclose(dump) != 0) return 4;
    std::printf("PASS CaptureSceneEqualityTest cases=%d rates=13 factors=3 checks=%llu\n", cases,
                static_cast<unsigned long long>(checks));
}
