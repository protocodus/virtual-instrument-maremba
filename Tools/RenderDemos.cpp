// Renders one demo per factory preset from its committed MIDI source,
// Docs/audio/midi/<NN>-<preset-slug>.mid, through MarembaPlayer: the MIDI
// front end the plug-in plays through. Every demo uses the same fixed sample
// rate and block size, so an unchanged engine renders identical bytes.
//
//   MarembaRenderDemos [--jobs N] [--readme README.md] <output-dir>
//       writes <NN>-<preset-slug>.wav per preset; with --readme, rewrites the
//       demo table between its audio-demos markers (the main build encodes
//       the WAVs to the .mp3 files the table names)
//   MarembaRenderDemos --check [--jobs N]
//       the audio tier: renders every preset twice and fails unless there is
//       one MIDI source per preset and every demo is finite, audible, below
//       the ceiling and byte-identical across the two renders

#include "DSP/MarembaPlayer.h"
#include "FactoryPresets.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using maremba::kFactoryPresets;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockFrames = 256;
constexpr double kMaxSeconds = 20.0;  // README demos are 10-20 s
constexpr double kTailSeconds = 2.5;  // after the last note-off, if it fits
constexpr double kFadeSeconds = 0.75; // only where kMaxSeconds cuts a tail
constexpr float kCeiling = 1.0f;      // 0 dBFS: the 24-bit file must not clip
constexpr float kAudibleFloor = 1.0e-3f;
const char* const kBegin = "<!-- audio-demos-begin -->";
const char* const kEnd = "<!-- audio-demos-end -->";

struct MidiEvent {
    double seconds;
    std::uint8_t bytes[3];
    int size;
};

struct Demo {
    std::string stem;
    std::vector<float> left, right;
    float peak = 0.0f;
};

std::string Slug(const std::string& name) {
    std::string slug;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!slug.empty() && slug.back() != '-')
            slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

std::string Stem(std::size_t preset) {
    char number[8];
    std::snprintf(number, sizeof number, "%02zu-", preset + 1);
    return number + Slug(kFactoryPresets[preset].name);
}

fs::path MidiDirectory() {
    return fs::path(MAREMBA_SOURCE_DIR) / "Docs/audio/midi";
}

// A Standard MIDI File (format 0 or 1) as channel messages in time order,
// with the tempo map applied. Meta and system-exclusive events are skipped.
std::vector<MidiEvent> ReadMidi(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    std::size_t at = 0;
    const auto need = [&](std::size_t n) {
        if (at + n > data.size()) throw std::runtime_error(path.string() + ": truncated");
    };
    const auto u16 = [&] { need(2); const int v = data[at] << 8 | data[at + 1]; at += 2; return v; };
    const auto u32 = [&] {
        need(4);
        const std::uint32_t v = std::uint32_t(data[at]) << 24 | std::uint32_t(data[at + 1]) << 16
            | std::uint32_t(data[at + 2]) << 8 | data[at + 3];
        at += 4;
        return v;
    };
    const auto vlq = [&] {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            need(1);
            const std::uint8_t b = data[at++];
            v = v << 7 | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        throw std::runtime_error(path.string() + ": bad variable-length quantity");
    };

    need(4);
    if (std::string(data.begin(), data.begin() + 4) != "MThd") throw std::runtime_error(path.string() + ": not a MIDI file");
    at = 4;
    const std::uint32_t headerLength = u32();
    const int format = u16(), tracks = u16(), division = u16();
    if (format > 1 || headerLength < 6 || (division & 0x8000) || division == 0)
        throw std::runtime_error(path.string() + ": needs format 0 or 1 with ticks per quarter note");
    at = 8 + headerLength;

    struct TickEvent { std::uint64_t tick; int order; std::uint32_t tempo; MidiEvent event; };
    std::vector<TickEvent> events;
    int order = 0;
    for (int track = 0; track < tracks; ++track) {
        need(8);
        if (std::string(data.begin() + static_cast<long>(at), data.begin() + static_cast<long>(at) + 4) != "MTrk")
            throw std::runtime_error(path.string() + ": missing MTrk");
        at += 4;
        const std::uint32_t length = u32();
        const std::size_t end = at + length;
        std::uint64_t tick = 0;
        std::uint8_t status = 0;
        while (at < end) {
            tick += vlq();
            need(1);
            if (data[at] & 0x80) status = data[at++];
            if (status == 0xFF) {
                need(1);
                const std::uint8_t type = data[at++];
                const std::uint32_t length = vlq();
                need(length);
                if (type == 0x51 && length == 3)
                    events.push_back({ tick, order++, std::uint32_t(data[at]) << 16 | std::uint32_t(data[at + 1]) << 8 | data[at + 2], {} });
                at += length;
                status = 0; // meta events cancel running status
            } else if (status == 0xF0 || status == 0xF7) {
                const std::uint32_t length = vlq();
                need(length);
                at += length;
                status = 0;
            } else if (status >= 0x80) {
                const int size = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 2 : 3;
                need(static_cast<std::size_t>(size - 1));
                MidiEvent event { 0.0, { status, data[at], size > 2 ? data[at + 1] : std::uint8_t(0) }, size };
                at += static_cast<std::size_t>(size - 1);
                events.push_back({ tick, order++, 0, event });
            } else {
                throw std::runtime_error(path.string() + ": data byte without a status");
            }
        }
        at = end;
    }

    std::stable_sort(events.begin(), events.end(), [](const TickEvent& a, const TickEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    std::vector<MidiEvent> result;
    double seconds = 0.0, secondsPerTick = 0.5 / division; // 120 bpm until a tempo event
    std::uint64_t lastTick = 0;
    for (const auto& e : events) {
        seconds += static_cast<double>(e.tick - lastTick) * secondsPerTick;
        lastTick = e.tick;
        if (e.tempo != 0) {
            secondsPerTick = e.tempo / 1.0e6 / division;
        } else {
            result.push_back(e.event);
            result.back().seconds = seconds;
        }
    }
    return result;
}

Demo Render(std::size_t preset) {
    const auto events = ReadMidi(MidiDirectory() / (Stem(preset) + ".mid"));
    if (events.empty()) throw std::runtime_error(Stem(preset) + ": no MIDI events");
    const double end = std::min(kMaxSeconds, events.back().seconds + kTailSeconds);
    const auto frames = static_cast<std::size_t>(std::lround(end * kSampleRate));

    maremba::MarembaPlayer player;
    player.Prepare(kSampleRate);
    player.SetControls(kFactoryPresets[preset].values);
    Demo demo { Stem(preset), std::vector<float>(frames), std::vector<float>(frames) };
    std::size_t next = 0, done = 0;
    while (done < frames) {
        const std::size_t blockEnd = std::min(frames, done + kBlockFrames);
        // Apply each event at its own sample, as the plug-in's processBlock does.
        while (done < blockEnd) {
            std::size_t until = blockEnd;
            if (next < events.size())
                until = std::min(until, static_cast<std::size_t>(std::llround(events[next].seconds * kSampleRate)));
            if (until > done) {
                player.Render(&demo.left[done], &demo.right[done], nullptr, nullptr, nullptr, nullptr, nullptr,
                              static_cast<int>(until - done));
                done = until;
            }
            while (next < events.size() && std::llround(events[next].seconds * kSampleRate) <= static_cast<long long>(done)) {
                player.HandleMidi(events[next].bytes, events[next].size);
                ++next;
            }
        }
    }

    if (end < events.back().seconds + kTailSeconds) {
        const auto fade = static_cast<std::size_t>(kFadeSeconds * kSampleRate);
        for (std::size_t i = 0; i < fade && i < frames; ++i) {
            const float gain = static_cast<float>(i) / static_cast<float>(fade);
            demo.left[frames - 1 - i] *= gain;
            demo.right[frames - 1 - i] *= gain;
        }
    }
    for (std::size_t i = 0; i < frames; ++i) {
        if (!std::isfinite(demo.left[i]) || !std::isfinite(demo.right[i])) {
            demo.peak = INFINITY;
            break;
        }
        demo.peak = std::max({ demo.peak, std::fabs(demo.left[i]), std::fabs(demo.right[i]) });
    }
    return demo;
}

std::vector<Demo> RenderAll(int jobs) {
    std::vector<Demo> demos(kFactoryPresets.size());
    std::atomic<std::size_t> next { 0 };
    std::vector<std::string> errors(demos.size());
    std::vector<std::thread> workers;
    for (int j = 0; j < std::max(1, jobs); ++j)
        workers.emplace_back([&] {
            for (std::size_t i; (i = next++) < demos.size();) {
                try {
                    demos[i] = Render(i);
                } catch (const std::exception& e) {
                    errors[i] = e.what();
                }
            }
        });
    for (auto& worker : workers) worker.join();
    for (const auto& error : errors)
        if (!error.empty()) throw std::runtime_error(error);
    return demos;
}

void WriteWav(const fs::path& path, const Demo& demo) {
    const auto frames = static_cast<std::uint32_t>(demo.left.size());
    const std::uint32_t dataBytes = frames * 2 * 3;
    std::vector<std::uint8_t> out;
    const auto put = [&](std::uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    out.insert(out.end(), { 'R', 'I', 'F', 'F' });
    put(36 + dataBytes, 4);
    out.insert(out.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    put(16, 4); put(1, 2); put(2, 2);
    put(static_cast<std::uint32_t>(kSampleRate), 4);
    put(static_cast<std::uint32_t>(kSampleRate) * 6, 4);
    put(6, 2); put(24, 2);
    out.insert(out.end(), { 'd', 'a', 't', 'a' });
    put(dataBytes, 4);
    for (std::uint32_t i = 0; i < frames; ++i)
        for (const float sample : { demo.left[i], demo.right[i] }) {
            const double scaled = std::round(std::clamp(static_cast<double>(sample), -1.0, 1.0) * 8388607.0);
            put(static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled)), 3);
        }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

void RewriteReadme(const fs::path& readme, const std::vector<Demo>& demos) {
    std::ifstream in(readme);
    std::stringstream text;
    text << in.rdbuf();
    const std::string source = text.str();
    const auto begin = source.find(kBegin), end = source.find(kEnd);
    if (begin == std::string::npos || end == std::string::npos || end < begin)
        throw std::runtime_error(readme.string() + ": missing the audio-demos markers");

    std::ostringstream table;
    table << kBegin << "\n\n| # | Preset | Demo | Length | Peak |\n| --- | --- | --- | --- | --- |\n";
    for (std::size_t i = 0; i < demos.size(); ++i) {
        char row[256];
        std::snprintf(row, sizeof row, "| %zu | %s | [%s.mp3](Docs/audio/%s.mp3) | %.1f s | %.1f dBFS |\n", i + 1,
                      kFactoryPresets[i].name, demos[i].stem.c_str(), demos[i].stem.c_str(),
                      static_cast<double>(demos[i].left.size()) / kSampleRate,
                      20.0 * std::log10(std::max(1.0e-9, static_cast<double>(demos[i].peak))));
        table << row;
    }
    table << "\n";
    std::ofstream(readme) << source.substr(0, begin) << table.str() << source.substr(end);
}

int Check(int jobs) {
    std::vector<std::string> expected, found;
    for (std::size_t i = 0; i < kFactoryPresets.size(); ++i) expected.push_back(Stem(i) + ".mid");
    for (const auto& entry : fs::directory_iterator(MidiDirectory())) found.push_back(entry.path().filename().string());
    std::sort(found.begin(), found.end());
    if (found != expected) {
        std::fprintf(stderr, "FAIL: Docs/audio/midi must hold exactly one <NN>-<preset-slug>.mid per factory preset\n");
        return 1;
    }

    const auto first = RenderAll(jobs);
    const auto second = RenderAll(jobs);
    int failures = 0;
    for (std::size_t i = 0; i < first.size(); ++i) {
        const auto& demo = first[i];
        const double seconds = static_cast<double>(demo.left.size()) / kSampleRate;
        const char* problem = !std::isfinite(demo.peak) ? "non-finite samples"
            : demo.peak < kAudibleFloor ? "silent"
            : demo.peak >= kCeiling ? "peak at or above 0 dBFS"
            : seconds < 10.0 || seconds > kMaxSeconds ? "not 10-20 s long"
            : demo.left != second[i].left || demo.right != second[i].right ? "a second render differs"
            : nullptr;
        std::printf("%s %s: %.1f s, peak %.2f dBFS%s%s\n", problem ? "FAIL" : "PASS", demo.stem.c_str(), seconds,
                    20.0 * std::log10(std::max(1.0e-9, static_cast<double>(demo.peak))),
                    problem ? ": " : "", problem ? problem : "");
        failures += problem != nullptr;
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        int jobs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        bool check = false;
        fs::path readme, output;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--check") check = true;
            else if (arg == "--jobs" && i + 1 < argc) jobs = std::stoi(argv[++i]);
            else if (arg == "--readme" && i + 1 < argc) readme = argv[++i];
            else if (!arg.empty() && arg[0] != '-' && output.empty()) output = arg;
            else throw std::runtime_error("usage: MarembaRenderDemos [--jobs N] [--readme README.md] <output-dir> | --check");
        }
        if (check) return Check(jobs);
        if (output.empty()) throw std::runtime_error("usage: MarembaRenderDemos [--jobs N] [--readme README.md] <output-dir>");

        const auto demos = RenderAll(jobs);
        fs::create_directories(output);
        for (const auto& demo : demos) {
            if (!std::isfinite(demo.peak)) throw std::runtime_error(demo.stem + ": non-finite samples");
            WriteWav(output / (demo.stem + ".wav"), demo);
            std::printf("%s.wav\n", demo.stem.c_str());
        }
        if (!readme.empty()) RewriteReadme(readme, demos);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
