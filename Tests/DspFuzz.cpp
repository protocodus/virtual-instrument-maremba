// libFuzzer harness for the public DSP API, through MarembaPlayer as the
// plug-in drives it: arbitrary control values (out-of-range, huge and
// nonfinite included), arbitrary MIDI bytes, damping, resets and render
// lengths at three sample rates. Every output sample must stay finite.
//
//   MarembaDspFuzz -max_total_time=120 <corpus-dir>
//
// A crash leaves a crash-* reproducer; once fixed, add its input to a unit
// contract.

#include "DSP/MarembaPlayer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

constexpr std::array<double, 3> kRates { 44100.0, 48000.0, 96000.0 };

struct Input {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t at = 0;

    bool empty() const { return at >= size; }
    std::uint8_t byte() { return at < size ? data[at++] : 0; }
    double number() {
        switch (byte() % 8) {
        case 0: return NAN;
        case 1: return byte() & 1 ? INFINITY : -INFINITY;
        case 2: return (byte() - 128) * 1.0e6;
        default: {
            std::uint16_t raw = byte();
            raw = static_cast<std::uint16_t>(raw << 8 | byte());
            return raw / 65535.0 * 1.4 - 0.2; // mostly 0..1, a little beyond
        }
        }
    }
};

void RequireFinite(const float* samples, int frames, const char* name) {
    for (int i = 0; i < frames; ++i)
        if (!std::isfinite(samples[i])) {
            std::fprintf(stderr, "non-finite %s sample\n", name);
            std::abort();
        }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // One player per rate: an engine allocates its delay lines up front.
    static std::array<std::unique_ptr<maremba::MarembaPlayer>, kRates.size()> players;
    Input in { data, size };
    const std::size_t rate = in.byte() % kRates.size();
    if (!players[rate]) {
        players[rate] = std::make_unique<maremba::MarembaPlayer>();
        players[rate]->Prepare(kRates[rate]);
    }
    auto& player = *players[rate];
    player.Reset();

    maremba::ControlValues controls = maremba::kControlDefaults;
    player.SetControls(controls);
    static std::array<float, 7 * 512> buffer;
    int rendered = 0;
    while (!in.empty() && rendered < 48000) {
        switch (in.byte() % 6) {
        case 0: {
            const int control = in.byte() % maremba::kControlCount;
            controls[static_cast<std::size_t>(control)] = in.number();
            player.SetControls(controls);
            break;
        }
        case 1:
        case 2: {
            const std::uint8_t message[3] = { static_cast<std::uint8_t>(0x80 | in.byte()), in.byte(), in.byte() };
            player.HandleMidi(message, 1 + in.byte() % 3);
            break;
        }
        case 3:
            player.DampAll();
            break;
        case 4:
            if (in.byte() < 8) player.Reset();
            break;
        default: {
            const int frames = 1 + (in.byte() << 1) % 512;
            float* out[7];
            for (int c = 0; c < 7; ++c) out[c] = buffer.data() + c * 512;
            // Direct outputs are optional: leave some unrendered.
            const std::uint8_t mask = in.byte();
            for (int c = 2; c < 7; ++c)
                if (!(mask & (1 << c))) out[c] = nullptr;
            player.Render(out[0], out[1], out[2], out[3], out[4], out[5], out[6], frames);
            for (int c = 0; c < 7; ++c)
                if (out[c]) RequireFinite(out[c], frames, "output");
            rendered += frames;
            break;
        }
        }
    }
    return 0;
}
