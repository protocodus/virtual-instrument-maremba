// Paired wall/thread-CPU engine probe. Run when other native builds have stopped.
// Compile both saved DSP trees with the same room-type capability definition.
// One cell: --case model quality room [sample-rate [measured-blocks]].
#include "MarembaEngine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const bool chordBurst = argc == 2 && std::string(argv[1]) == "--chord-burst";
    const bool allModels = argc == 2 && std::string(argv[1]) == "--all-models";
    const bool maxBuzz = argc == 2 && std::string(argv[1]) == "--max-buzz";
    const bool oneCase = (argc == 5 || argc == 6 || argc == 7) && std::string(argv[1]) == "--case";
    if (argc > 1 && !allModels && !chordBurst && !maxBuzz && !oneCase) return 2;
    const int selectedModel = oneCase ? std::stoi(argv[2]) : 0;
    const int selectedQuality = oneCase ? std::stoi(argv[3]) : 3;
    const int selectedRoom = oneCase ? std::stoi(argv[4]) : 0;
    const int rate = oneCase && argc >= 6 ? std::stoi(argv[5]) : 48000;
    const int count = oneCase && argc == 7 ? std::stoi(argv[6]) : 4096;
    constexpr int frames=64;
    if (selectedModel < 0 || selectedModel > 3 || selectedQuality < 0 || selectedQuality > 3 ||
        selectedRoom < 0 || selectedRoom > 2 || rate < 22050 || rate > 192000 || count < 256) return 2;
    volatile float sink=0;
#ifdef MAREMBA_HAS_ROOM_TYPES
    constexpr int rooms=3;
#else
    constexpr int rooms=1;
#endif
    for (int model=oneCase ? selectedModel : 0;model<(oneCase ? selectedModel+1 : (allModels || chordBurst || maxBuzz) ? 4 : 1);++model)
    for (int quality=oneCase ? selectedQuality : allModels ? 0 : 3;quality<(oneCase ? selectedQuality+1 : 4);++quality)
    for (int room=oneCase ? selectedRoom : 0;room<(oneCase ? selectedRoom+1 : (allModels || chordBurst || maxBuzz) ? 1 : rooms);++room) {
        maremba::MarembaEngine engine(rate);
        maremba::EngineParameters p;
        p.model=model; p.oversampling=quality;
        if (maxBuzz) p.buzzAmount=1;
        const float velocity=maxBuzz ? 1.f : .8f;
#ifdef MAREMBA_HAS_ROOM_TYPES
        p.roomType=room;
#endif
        engine.SetParameters(p);
        for (int n=0;n<16;++n) engine.NoteOn(42+n,velocity);
        std::vector<double> wall,cpu; wall.reserve(count);cpu.reserve(count);
        std::array<std::array<float,frames>,7> out{};
        int wallMiss=0,cpuMiss=0;
        for (int block=-256;block<count;++block) {
            timespec a{},b{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&a);
            const auto start=std::chrono::steady_clock::now();
            engine.SetParameters(p);
            if (block>=0 && block%32==0) {
                if (chordBurst) {
                    for (int n=0;n<16;++n) engine.NoteOn(42+n,velocity);
                } else engine.NoteOn(42+(block/32)%16,velocity);
            }
            engine.RenderBatch(out[0].data(),out[1].data(),out[2].data(),out[3].data(),out[4].data(),out[5].data(),out[6].data(),frames);
            const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
            clock_gettime(CLOCK_THREAD_CPUTIME_ID,&b);
            const double charged=1e6*(b.tv_sec-a.tv_sec)+1e-3*(b.tv_nsec-a.tv_nsec);
            sink=sink+out[0][0];
            if(block<0)continue;
            wall.push_back(elapsed);cpu.push_back(charged);
            wallMiss+=elapsed>1e6*frames/rate;cpuMiss+=charged>1e6*frames/rate;
        }
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        constexpr int factors[] = {2, 4, 8, 1};
        std::cout << std::setprecision(10) << rate << "Hz / 64 frames / 16 voices / model " << model << " / " << factors[quality] << "x / room " << room
                  << (chordBurst ? " / 16-note restrike burst every32 blocks" : "")
                  << (maxBuzz ? " / maximum Buzz and velocity" : "")
                  << " / " << count << " blocks: wall_us p50=" << wall[count/2]
                  << " p95=" << wall[count*95/100] << " p99=" << wall[count*99/100] << " max=" << wall.back()
                  << "; thread_cpu_us p50=" << cpu[count/2] << " p95=" << cpu[count*95/100] << " p99=" << cpu[count*99/100]
                  << " max=" << cpu.back() << "; deadline_misses wall=" << wallMiss << " cpu=" << cpuMiss
                  << "; mean_us wall=" << std::accumulate(wall.begin(),wall.end(),0.0)/count
                  << " cpu=" << std::accumulate(cpu.begin(),cpu.end(),0.0)/count << '\n';
    }
    (void)sink;
}
