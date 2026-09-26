// Binary float32 transport for measure_output_headroom.py. Processes the actual
// OutputHeadroom header; checks all four linked groups before writing main L.
#include "OutputHeadroom.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    const int frames = std::atoi(argv[3]);
    const double rate = std::atof(argv[4]);
    if (frames < 1 || rate < 22050 || rate > 192000) return 2;
    FILE* input = std::fopen(argv[1], "rb");
    FILE* output = std::fopen(argv[2], "wb");
    if (!input || !output) return 3;
    std::vector<float> x(frames), y(frames);
    while (true) {
        const auto count = std::fread(x.data(), sizeof(float), x.size(), input);
        if (count == 0) break;
        if (count != x.size()) return 4;
        maremba::OutputHeadroom headroom;
        headroom.SetSampleRate(rate);
        for (int i = 0; i < frames; ++i) {
            const float right = -.37f * x[i];
            std::array<float, 7> frame{{x[i], right, x[i], right, x[i], right, x[i]}};
            headroom.Process(frame);
            for (float v : frame)
                if (!std::isfinite(v) || std::abs(v) > .940002f) return 5;
            if (frame[0] != frame[2] || frame[0] != frame[4] || frame[0] != frame[6]
                || frame[1] != frame[3] || frame[1] != frame[5]) return 6;
            y[i] = frame[0];
        }
        if (std::fwrite(y.data(), sizeof(float), y.size(), output) != y.size()) return 7;
    }
    if (std::ferror(input)) return 8;
    const int inputClose = std::fclose(input), outputClose = std::fclose(output);
    return inputClose || outputClose;
}
