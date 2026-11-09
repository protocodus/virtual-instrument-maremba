// Perf tier only: the batch in which the engine falls idle costs no more than
// a ringing batch. Up to 1.6.1 that batch cleared every capture source's
// buffers at once (about 16 MB) and took about twice Reason's whole 64-frame
// budget, a dropout seconds after every phrase died away, while the
// per-render averages the other perf checks report hardly moved.
//
// Protocol: Init controls at 44.1 kHz in Reason's 64-frame batches, one key
// struck and held 0.5 s, rendered until the engine reports silence. Each
// batch is timed with the steady clock; three fresh engines play the same
// deterministic script and each batch keeps its minimum, so a scheduler
// preemption does not read as a DSP spike. The gate compares the idle batch
// with the median batch of the ring 0.5-2.5 s after the strike, so it holds
// on any machine.
#include "MarembaEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
using namespace maremba;

constexpr double kRate = 44100.0;
constexpr int kBatch = 64;
constexpr int kRuns = 3;
constexpr int kPerSecond = static_cast<int>(kRate / kBatch);
constexpr int kStrike = kPerSecond / 4;
constexpr int kRelease = kStrike + kPerSecond / 2;
constexpr int kLimit = 40 * kPerSecond;

// Returns the batch in which the engine fell idle, or -1.
int Play(std::vector<double>& best) {
    auto engine = std::make_unique<MarembaEngine>(kRate);
    std::array<float, kBatch> left{}, right{};
    for (int b = 0; b < kLimit; ++b) {
        if (b == kStrike) engine->NoteOn(60, 0.8f);
        if (b == kRelease) engine->NoteOff(60);
        const auto t0 = std::chrono::steady_clock::now();
        engine->RenderBatch(left.data(), right.data(), nullptr, nullptr, nullptr, nullptr, nullptr, kBatch);
        const std::chrono::duration<double, std::micro> spent = std::chrono::steady_clock::now() - t0;
        best[static_cast<std::size_t>(b)] = std::min(best[static_cast<std::size_t>(b)], spent.count());
        if (b > kRelease && engine->IsSilent()) return b;
    }
    return -1;
}
} // namespace

int main() {
    std::vector<double> best(kLimit, 1e300);
    int idle = -1;
    for (int run = 0; run < kRuns; ++run) {
        const int at = Play(best);
        if (at < 0 || (idle >= 0 && at != idle)) {
            std::printf("IdleFlushCostTest FAIL: the engine went idle at batch %d after batch %d\n", at, idle);
            return 1;
        }
        idle = at;
    }
    std::vector<double> ring(best.begin() + kStrike + kPerSecond / 2, best.begin() + kStrike + 5 * kPerSecond / 2);
    std::nth_element(ring.begin(), ring.begin() + ring.size() / 2, ring.end());
    const double median = ring[ring.size() / 2];
    const double flush = best[static_cast<std::size_t>(idle)];
    std::printf("IDLE_FLUSH idle_at=%.2f s ring_median=%.1f us idle_batch=%.1f us budget=%.0f us\n",
                idle / static_cast<double>(kPerSecond), median, flush, kBatch / kRate * 1e6);
    if (!(flush <= 2.0 * median)) {
        std::printf("IdleFlushCostTest FAIL: falling idle costs %.1f us, over twice a ringing batch (%.1f us)\n",
                    flush, median);
        return 1;
    }
    std::printf("IdleFlushCostTest PASS\n");
    return 0;
}
