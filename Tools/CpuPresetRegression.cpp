// CPU regression stream over the factory presets: one fixed workload for
// counting instructions and for proving an optimisation leaves the sound
// unchanged. File I/O and allocation stay outside the engine's render calls.
//
// Measurement protocol (Docs/benchmarks/cpu-capture-scene-2026-10-25.md):
//
//   Build the engine as the Rack compiles it, as nearly as the host allows
//   (C++17, -O3 -fno-builtin; Maremba has no embedded define), and once more
//   as a normal host -O3 build:
//
//     clang++ -std=c++17 -O3 -fno-builtin -DNDEBUG -ISource -ISource/DSP \
//       Tools/CpuPresetRegression.cpp Source/DSP/MarembaEngine.cpp \
//       Source/DSP/MarembaVoice.cpp -o cpu_rack
//     clang++ -std=c++17 -O3 -DNDEBUG ... -o cpu_host
//
//   Count instructions with callgrind; the counts are reproducible on a
//   shared machine where timings are not:
//
//     valgrind --tool=callgrind --toggle-collect='*RenderStream*' \
//       --callgrind-out-file=cg.out ./cpu_rack main
//
//   and take the total Ir: the rendered stream, MIDI handling included, and
//   not the engine's construction, which the Rack does off the audio thread.
//
//   Parity: run `./cpu_rack all out.f32` and `./cpu_rack main out.f32` on the
//   baseline and the candidate (same compiler and flags) and compare the
//   files byte for byte, together with the outputs of
//   Tools/OptimizationRender.cpp and Tools/CpuEquivalenceRender.cpp (rates,
//   qualities, rooms, voice steals and live control changes). Each candidate is graded exact (same double bits),
//   last bit (float32 stream byte-identical) or measured inaudible (residual
//   >= 90 dB under signal RMS on every preset, no residual peak above
//   -80 dBFS); anything else is a listening decision and does not ship as an
//   optimisation.
//
// Workload: every factory preset at its stored controls (1x quality, 16
// voices), 48 kHz, 64-frame calls, 1.6 s each: an eight-note melody at
// varied velocity, a held four-note chord with a pitch-bend move, a
// sixteen-note cluster struck together, released, and its decay. Then the
// heaviest supported quality (8x) on one preset per model, the first 0.6 s
// of the same phrase. "main" connects the stereo Main outputs only; "all"
// connects all seven outputs.
// Usage: cpu_preset_regression main|all [output.f32]
#include "DSP/MarembaPlayer.h"
#include "FactoryPresets.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using maremba::MarembaPlayer;

constexpr int kRate = 48000;
constexpr int kBlock = 64;

struct Event { int frame; std::uint8_t status, a, b; };

// Note events of the phrase, at 48 kHz frames; the list is in frame order.
constexpr Event kPhrase[] = {
    {0, 0x90, 60, 70}, {2400, 0x80, 60, 0}, {2400, 0x90, 64, 100}, {4800, 0x80, 64, 0},
    {4800, 0x90, 67, 45}, {7200, 0x90, 72, 120}, {8000, 0x80, 67, 0}, {9600, 0x80, 72, 0},
    {9600, 0x90, 71, 85}, {12000, 0x90, 69, 60}, {13200, 0x80, 71, 0}, {14400, 0x90, 65, 127},
    {16000, 0x80, 69, 0}, {16800, 0x90, 62, 30}, {19200, 0x80, 65, 0}, {19200, 0x80, 62, 0},
    // Held chord, a bend up and back.
    {28800, 0x90, 48, 90}, {28800, 0x90, 55, 80}, {28800, 0x90, 64, 75}, {28800, 0x90, 70, 85},
    {33600, 0xE0, 0x00, 0x50}, {38400, 0xE0, 0x00, 0x40},
    {48000, 0x80, 48, 0}, {48000, 0x80, 55, 0}, {48000, 0x80, 64, 0}, {48000, 0x80, 70, 0},
    // Sixteen-note cluster, notes 42..57.
    {49600, 0x90, 42, 96}, {49600, 0x90, 43, 80}, {49600, 0x90, 44, 110}, {49600, 0x90, 45, 70},
    {49600, 0x90, 46, 100}, {49600, 0x90, 47, 90}, {49600, 0x90, 48, 85}, {49600, 0x90, 49, 120},
    {49600, 0x90, 50, 64}, {49600, 0x90, 51, 75}, {49600, 0x90, 52, 105}, {49600, 0x90, 53, 95},
    {49600, 0x90, 54, 88}, {49600, 0x90, 55, 78}, {49600, 0x90, 56, 115}, {49600, 0x90, 57, 100},
    {64000, 0xB0, 123, 0},
};

// The rendered stream alone, kept out of line so callgrind can count it
// without the engine's construction (--toggle-collect='*RenderStream*').
__attribute__((noinline)) void RenderStream(MarembaPlayer& player, int frames, float* const* p, FILE* out) {
    std::size_t next = 0;
    constexpr std::size_t kEvents = sizeof(kPhrase) / sizeof(kPhrase[0]);
    for (int frame = 0; frame < frames; frame += kBlock) {
        const int n = frames - frame < kBlock ? frames - frame : kBlock;
        // Events start at the block holding their frame (64-frame resolution).
        while (next < kEvents && kPhrase[next].frame < frame + n) {
            const std::uint8_t msg[3] = {kPhrase[next].status, kPhrase[next].a, kPhrase[next].b};
            player.HandleMidi(msg, 3);
            ++next;
        }
        player.Render(p[0], p[1], p[2], p[3], p[4], p[5], p[6], n);
        if (out)
            for (int i = 0; i < 7; ++i)
                if (p[i]) std::fwrite(p[i], sizeof(float), static_cast<std::size_t>(n), out);
    }
}

void Play(const maremba::ControlValues& controls, bool heaviest, int frames, bool all, FILE* out) {
    static MarembaPlayer player;
    maremba::ControlValues values = controls;
    if (heaviest) values[maremba::kOversampling] = 2.0;   // stored index 2: 8x
    player.Prepare(kRate);   // allocates: construction, outside the counted stream
    player.SetControls(values);
    std::array<std::array<float, kBlock>, 7> buffers {};
    float* p[7];
    for (int i = 0; i < 7; ++i) p[i] = (all || i < 2) ? buffers[static_cast<std::size_t>(i)].data() : nullptr;
    RenderStream(player, frames, p, out);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    const bool all = std::strcmp(argv[1], "all") == 0;
    if (!all && std::strcmp(argv[1], "main") != 0) return 2;
    FILE* out = argc == 3 ? std::fopen(argv[2], "wb") : nullptr;
    if (argc == 3 && !out) return 3;
    for (const auto& preset : maremba::kFactoryPresets)
        Play(preset.values, false, 8 * kRate / 5, all, out);
    // The heaviest supported quality: the first preset of each model.
    int lastModel = -1;
    for (const auto& preset : maremba::kFactoryPresets) {
        const int model = static_cast<int>(preset.values[maremba::kModel]);
        if (model == lastModel) continue;
        lastModel = model;
        Play(preset.values, true, 3 * kRate / 5, all, out);
    }
    if (out) std::fclose(out);
    return 0;
}
