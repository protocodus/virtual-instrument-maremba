// Input fuzzing of the public DSP API (U4), through MarembaPlayer as the
// plug-in drives it: arbitrary control values (out-of-range, huge and
// nonfinite included), arbitrary MIDI bytes, damping, resets, host sample
// rates (unsupported ones included) and render lengths. Every rendered sample
// must stay finite and every write inside the caller's frame count.
//
//   MarembaDspFuzz -max_total_time=120 <corpus-dir>   (libFuzzer, Clang)
//   MarembaDspFuzzReplay [-seed=N] [-runs=N] [file ...] (any compiler)
//
// A crash leaves a crash-* reproducer; once fixed, commit it under
// Tests/fuzz-regressions/ so the unit tier replays it.

#include "DSP/MarembaPlayer.h"
#include "FuzzHarness.h"

#include <array>
#include <memory>

namespace {
constexpr std::size_t kMaxFrames = 512;
constexpr std::size_t kDirectPairs = 3; // close, far and piezo (left only)
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace protocodus::fuzz;
    Input input { data, size };
    auto player = std::make_unique<maremba::MarembaPlayer>();
    player->Prepare(input.Rate());

    maremba::ControlValues controls = maremba::kControlDefaults;
    player->SetControls(controls);
    auto main = std::make_unique<Sentinels<kMaxFrames>>();
    auto direct = std::make_unique<std::array<Sentinels<kMaxFrames>, kDirectPairs>>();
    const auto render = [&](std::size_t frames, std::uint8_t mask) {
        // Direct outputs are optional: leave some unrendered.
        const auto use = [&](std::size_t pair) { return (mask >> pair & 1u) != 0; };
        auto& d = *direct;
        main->Arm();
        for (auto& pair : d) pair.Arm();
        player->Render(main->Left(), main->Right(),
                       use(0) ? d[0].Left() : nullptr, use(0) ? d[0].Right() : nullptr,
                       use(1) ? d[1].Left() : nullptr, use(1) ? d[1].Right() : nullptr,
                       use(2) ? d[2].Left() : nullptr, static_cast<int>(frames));
        main->Check(frames);
        for (std::size_t pair = 0; pair < kDirectPairs; ++pair) d[pair].Check(use(pair) ? frames : 0);
    };

    for (int command = 0; command < 64 && !input.Empty(); ++command) {
        switch (input.Pick(6)) {
            case 0:
                controls[input.Pick(maremba::kControlCount)] = input.Scalar();
                player->SetControls(controls);
                break;
            case 1:
            case 2: {
                const std::uint8_t message[3] { static_cast<std::uint8_t>(0x80 | input.Byte()), input.Byte(), input.Byte() };
                player->HandleMidi(message, 1 + static_cast<int>(input.Pick(3)));
                break;
            }
            case 3: player->DampAll(); break;
            case 4: player->Reset(); break;
            default: if (input.Byte() < 8) player->Prepare(input.Rate()); break;
        }
        render(1 + input.Pick(kMaxFrames), input.Byte());
    }
    return 0;
}
