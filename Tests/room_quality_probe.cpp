// Production RoomCapture wet quality probe. The existing friend diagnostic
// removes only the direct tap gain; production headers compile unchanged.
// Receivers, six early reflections and both late-field networks remain active.
#include "RoomCapture.h"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <time.h>
#include <vector>

namespace maremba {
struct RoomCaptureTestAccess {
    static void ExcludeDirect(RoomCapture& room) {
        for (auto& source : room.mSource) for (auto& tap : source.direct) tap.gain = 0;
    }
    static double LateArrival(int type, CapturePoint source, int channel) {
        const auto voicing = RoomCapture::Voicing(type);
        return RoomCapture::LateFeedSeconds(type, source, channel)
             + *std::min_element(voicing.lines.begin(), voicing.lines.end());
    }
};
}

namespace {
using Stereo = std::array<float, 2>;
double cpuClock() {
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return double(time.tv_sec) + double(time.tv_nsec) * 1.e-9;
}
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: room-quality-probe OUTPUT_DIR SAMPLE_RATE ROOM_TYPE\n";
        return 2;
    }
    const std::string output = argv[1];
    const int rate = std::stoi(argv[2]), type = std::stoi(argv[3]);
    if ((rate != 44100 && rate != 48000 && rate != 96000) || type < 0 || type > 2) return 2;
    constexpr int seconds = 8;
    const int frames = seconds * rate;
    auto room = std::make_unique<maremba::RoomCapture>();
    room->Allocate(rate, 1); room->SetSampleRate(rate); room->SetType(type);
    const maremba::CapturePoint source{.2, -.15, .04};
    if (!room->AddSource(0, source)) return 3;
    maremba::RoomCaptureTestAccess::ExcludeDirect(*room);
    const auto decay = maremba::RoomCapture::Decay(type, rate);
    std::vector<Stereo> audio(static_cast<std::size_t>(frames));
    std::vector<double> cpu;
    bool finite = true, repeatable = true, silentAfterReset = true;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) room->Reset();
        for (int start = 0; start < frames; start += 64) {
            const double before = cpuClock();
            for (int frame = start; frame < std::min(start + 64, frames); ++frame) {
                const float input = frame == 0 ? .25f : 0.f;
                Stereo sample{};
                room->Process(&input, 1, sample[0], sample[1]);
                finite = finite && std::isfinite(sample[0]) && std::isfinite(sample[1]);
                if (pass) repeatable = repeatable && sample == audio[static_cast<std::size_t>(frame)];
                else audio[static_cast<std::size_t>(frame)] = sample;
            }
            if (!pass) cpu.push_back((cpuClock() - before) * 1.e6);
        }
    }
    room->Reset();
    for (int frame = 0; frame < 1024; ++frame) {
        float left = 0, right = 0;
        room->Process(nullptr, 0, left, right);
        silentAfterReset = silentAfterReset && left == 0 && right == 0;
    }
    const std::string stem = output + "/room" + std::to_string(type) + "-" + std::to_string(rate);
    std::ofstream audioFile(stem + ".f32", std::ios::binary);
    audioFile.write(reinterpret_cast<const char*>(audio.data()), audio.size() * sizeof(Stereo));
    audioFile.close();
    if (!audioFile) return 4;
    std::ofstream timing(stem + "-cpu.csv");
    timing << "block,cpu_us\n";
    for (std::size_t i = 0; i < cpu.size(); ++i) timing << i << ',' << cpu[i] << '\n';
    timing.close();
    if (!timing) return 4;
    std::cout << std::setprecision(12) << std::boolalpha
        << "{\"room\":" << type << ",\"sample_rate\":" << rate << ",\"frames\":" << frames
        << ",\"input_impulse\":0.25,\"source_position\":[0.2,-0.15,0.04],\"direct_excluded\":true"
        << ",\"finite\":" << finite << ",\"repeatable\":" << repeatable << ",\"silent_after_reset\":" << silentAfterReset
        << ",\"late_first_arrival_seconds\":[" << maremba::RoomCaptureTestAccess::LateArrival(type, source, 0)
        << ',' << maremba::RoomCaptureTestAccess::LateArrival(type, source, 1)
        << "],\"decay_targets\":[" << decay.t125 << ',' << decay.t500 << ',' << decay.t4k << ',' << decay.t10k << "]}\n";
    return finite && repeatable && silentAfterReset ? 0 : 1;
}
