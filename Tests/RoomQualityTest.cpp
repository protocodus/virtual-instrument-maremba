// Room quality contract: impulse and pink-noise measurements of the three rooms
// (RoomBank types 0 Wood, 1 Chamber, 2 Hall) at internal rates 88.2, 96, 192 and
// 384 kHz, wet output only.
//
//   clang++ -std=c++17 -O3 -Wall -Wextra -Werror -I Examples/Maremba/DSP \
//       Examples/Maremba/Tests/RoomQualityTest.cpp -o /tmp/room_quality && /tmp/room_quality
//
// Checks per room and rate: broadband T60 (Schroeder backward integral, linear
// fit from -5 to -35 dB) and third-octave band T60s near their targets; normalized
// echo density (Abel & Huang 2006, 20 ms Hann window, mean over 50-150 ms) and
// tail spectral roughness (std of the Hann-windowed 100-600 ms spectrum minus its
// 501-bin moving average, 200 Hz-4 kHz; white noise gives about 5.6 dB) per
// channel; left/right tail correlation; the long-term wet level for a pink-noise
// burst against the implementation before this change; rate invariance against
// 88.2 kHz; finite output for extreme finite input; exact silence after Reset.
// Exits non-zero on any failure.

#include "RoomAlgorithms.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRooms = 3, kRatesCount = 4, kBands = 4;
const double kRates[kRatesCount] = { 88200.0, 96000.0, 192000.0, 384000.0 };
const char* const kNames[kRooms] = { "Wood", "Chamber", "Hall" };

// Long-term wet level (dB re full scale) of each room for PinkBurst below at
// -20 dBFS RMS, measured over 3 s with this program against the rooms before
// this change (1.3.0f5: Wood = four damped combs and two allpasses per side,
// Chamber = 8-line Householder FDN, Hall = the same tank without an input band
// limit) at 88.2/96/192/384 kHz. The rooms must keep their loudness within 1 dB.
const double kPreChangeLevelDb[kRooms][kRatesCount] = {
    { -13.24, -13.18, -13.27, -13.15 },
    { -29.87, -29.75, -29.83, -29.75 },
    { -25.47, -25.35, -25.28, -25.23 },
};

struct Band { double hz, seconds, tolerance; };
struct Targets {
    double t60, t60Tolerance; // broadband
    Band bands[kBands];       // third-octave
    double minEchoDensity, maxRoughnessDb;
};
const Targets kTargets[kRooms] = {
    // Wood: warm timber room, T60 1.0 s at 125 Hz (panels), 1.1 s mid, 0.75 s at 4 kHz, 0.45 s at 10 kHz
    { 1.00, 0.10, { { 125, 1.00, 0.10 }, { 500, 1.10, 0.10 }, { 4000, 0.75, 0.10 }, { 10000, 0.45, 0.15 } }, 0.8, 7.0 },
    // Chamber: 0.85 s through the mids, darker highs (the band-limited 10 kHz band reads slightly long)
    { 0.80, 0.10, { { 125, 0.85, 0.10 }, { 500, 0.85, 0.10 }, { 4000, 0.40, 0.15 }, { 10000, 0.20, 0.20 } }, 0.8, 7.0 },
    // Hall: decay character unchanged (band T60s of the implementation before this change)
    { 2.75, 0.10, { { 125, 3.20, 0.05 }, { 500, 3.28, 0.05 }, { 4000, 2.06, 0.05 }, { 10000, 1.30, 0.05 } }, 0.6, 7.0 },
};
constexpr double kMaxCorrelation = 0.3;
constexpr double kLevelToleranceDb = 1.0;
constexpr double kRateT60Tolerance = 0.05, kRateLevelToleranceDb = 0.5;

int gFailures = 0;

void Check(bool ok, const char* room, double rate, const char* what, double value, double limit) {
    if (ok) return;
    ++gFailures;
    std::printf("  FAIL %s @ %.0f Hz: %s = %.4f (limit %.4f)\n", room, rate, what, value, limit);
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
    double Process(double x) {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    static Biquad BandPass(double fs, double f0, double q) {
        const double w = 2 * kPi * f0 / fs, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        Biquad b;
        b.b0 = alpha / a0; b.b2 = -alpha / a0; b.a1 = -2 * std::cos(w) / a0; b.a2 = (1 - alpha) / a0;
        return b;
    }
    static Biquad Pass(double fs, double f0, double q, bool high) {
        const double w = 2 * kPi * f0 / fs, alpha = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + alpha;
        Biquad b;
        b.b0 = (high ? 1 + c : 1 - c) / 2 / a0; b.b1 = (high ? -(1 + c) : 1 - c) / a0; b.b2 = b.b0;
        b.a1 = -2 * c / a0; b.a2 = (1 - alpha) / a0;
        return b;
    }
};

// Schroeder backward integral of an energy envelope; T60 from the least-squares
// slope of its level between -5 and -35 dB.
double T60(const std::vector<double>& energy, double fs) {
    const size_t n = energy.size();
    std::vector<double> edc(n);
    double acc = 0;
    for (size_t i = n; i-- > 0;) { acc += energy[i]; edc[i] = acc; }
    if (!(edc[0] > 0)) return 0;
    size_t i0 = 0;
    while (i0 < n && 10 * std::log10(edc[i0] / edc[0]) > -5) ++i0;
    size_t i1 = i0;
    while (i1 < n && 10 * std::log10(edc[i1] / edc[0]) > -35) ++i1;
    if (i1 >= n || i1 < i0 + 3) return 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double count = static_cast<double>(i1 - i0);
    for (size_t i = i0; i < i1; ++i) {
        const double x = static_cast<double>(i) / fs, y = 10 * std::log10(edc[i] / edc[0]);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    return -60.0 / ((count * sxy - sx * sy) / (count * sxx - sx * sx));
}

double BroadbandT60(const std::vector<float>& l, const std::vector<float>& r, double fs) {
    std::vector<double> e(l.size());
    for (size_t i = 0; i < l.size(); ++i) e[i] = double(l[i]) * l[i] + double(r[i]) * r[i];
    return T60(e, fs);
}

// Third-octave band (two cascaded band-passes, Q 4.32) of both channels.
double BandT60(const std::vector<float>& l, const std::vector<float>& r, double fs, double hz) {
    std::vector<double> e(l.size(), 0.0);
    for (const auto* ch : { &l, &r }) {
        Biquad a = Biquad::BandPass(fs, hz, 4.32), b = Biquad::BandPass(fs, hz, 4.32);
        for (size_t i = 0; i < ch->size(); ++i) {
            const double y = b.Process(a.Process((*ch)[i]));
            e[i] += y * y;
        }
    }
    return T60(e, fs);
}

// Abel & Huang normalized echo density: the weighted fraction of samples beyond
// one standard deviation in a 20 ms Hann window, divided by the Gaussian value
// erfc(1/sqrt(2)); averaged over window centres from t0 to t1 (every 0.25 ms).
double EchoDensity(const std::vector<float>& h, double fs, double t0, double t1) {
    const int half = static_cast<int>(std::lround(0.010 * fs)), length = 2 * half + 1;
    std::vector<double> w(static_cast<size_t>(length));
    double sum = 0;
    for (int i = 0; i < length; ++i) { w[size_t(i)] = 0.5 - 0.5 * std::cos(2 * kPi * (i + 1) / (length + 1)); sum += w[size_t(i)]; }
    for (auto& x : w) x /= sum;
    const double gaussian = std::erfc(1.0 / std::sqrt(2.0));
    const int hop = std::max(1, static_cast<int>(std::lround(0.00025 * fs)));
    double acc = 0;
    int count = 0;
    for (int c = static_cast<int>(std::lround(t0 * fs)); c <= static_cast<int>(std::lround(t1 * fs)); c += hop) {
        double variance = 0;
        for (int i = 0; i < length; ++i) { const double v = h[size_t(c - half + i)]; variance += w[size_t(i)] * v * v; }
        const double sigma = std::sqrt(variance);
        double beyond = 0;
        for (int i = 0; i < length; ++i) if (std::abs(double(h[size_t(c - half + i)])) > sigma) beyond += w[size_t(i)];
        acc += beyond / gaussian;
        ++count;
    }
    return acc / count;
}

// Mixed-radix DFT (the segment lengths used here factor into 2, 3, 5 and 7).
void Fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    if (n <= 1) return;
    size_t p = 2;
    while (n % p) { ++p; if (p * p > n) { p = n; break; } }
    const size_t m = n / p;
    std::vector<std::vector<std::complex<double>>> sub(p, std::vector<std::complex<double>>(m));
    for (size_t r = 0; r < p; ++r) for (size_t k = 0; k < m; ++k) sub[r][k] = a[k * p + r];
    for (auto& s : sub) Fft(s);
    for (size_t k = 0; k < n; ++k) {
        const std::complex<double> t = std::polar(1.0, -2.0 * kPi * double(k) / double(n));
        std::complex<double> twiddle = 1.0, acc = 0.0;
        for (size_t r = 0; r < p; ++r) { acc += sub[r][k % m] * twiddle; twiddle *= t; }
        a[k] = acc;
    }
}

// Spectral roughness of the 100-600 ms tail (dB).
double Roughness(const std::vector<float>& h, double fs) {
    const size_t from = size_t(std::lround(0.1 * fs)), n = size_t(std::lround(0.6 * fs)) - from;
    std::vector<std::complex<double>> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = double(h[from + i]) * (0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n - 1)));
    Fft(x);
    const size_t bins = n / 2 + 1;
    std::vector<double> db(bins), prefix(bins + 1, 0.0);
    for (size_t k = 0; k < bins; ++k) { db[k] = 20 * std::log10(std::abs(x[k]) + 1e-30); prefix[k + 1] = prefix[k] + db[k]; }
    double s = 0, ss = 0;
    int count = 0;
    for (size_t k = 0; k < bins; ++k) {
        const double hz = double(k) * fs / double(n);
        if (hz < 200 || hz > 4000) continue;
        const size_t lo = k >= 250 ? k - 250 : 0, hi = std::min(bins, k + 251);
        const double d = db[k] - (prefix[hi] - prefix[lo]) / double(hi - lo);
        s += d; ss += d * d; ++count;
    }
    const double mean = s / count;
    return std::sqrt(ss / count - mean * mean);
}

double Correlation(const std::vector<float>& l, const std::vector<float>& r, double fs) {
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = size_t(0.1 * fs); i < size_t(0.6 * fs); ++i) {
        lr += double(l[i]) * r[i]; ll += double(l[i]) * l[i]; rr += double(r[i]) * r[i];
    }
    return lr / std::sqrt(ll * rr + 1e-300);
}

// Deterministic pink-noise burst with the same spectrum in Hz at every rate:
// Gaussian white noise, Kellett's economy pinking filter re-expressed as
// unity-DC one-pole sections in Hz, 20 Hz high-pass, 16 kHz fourth-order
// low-pass, normalised to the given RMS.
std::vector<float> PinkBurst(double fs, double seconds, double rms) {
    const size_t n = size_t(std::lround(seconds * fs));
    std::vector<double> y(n);
    uint64_t s = 0x9E3779B97F4A7C15ull;
    auto uniform = [&]() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return double((s * 2685821657736338717ull) >> 11) * (1.0 / 9007199254740992.0);
    };
    const double corner[3] = { 16.5, 264.6, 3946.0 }, weight[3] = { 42.15, 8.014, 2.448 };
    double pole[3], state[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; ++k) pole[k] = std::exp(-2 * kPi * corner[k] / fs);
    Biquad hp = Biquad::Pass(fs, 20, 0.7071, true), lp1 = Biquad::Pass(fs, 16000, 0.5412, false), lp2 = Biquad::Pass(fs, 16000, 1.3066, false);
    for (size_t i = 0; i < n; ++i) {
        const double u1 = std::max(uniform(), 1e-300), u2 = uniform();
        const double g = std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
        double v = 0.1848 * g;
        for (int k = 0; k < 3; ++k) { state[k] = pole[k] * state[k] + (1 - pole[k]) * g; v += weight[k] * state[k]; }
        y[i] = lp2.Process(lp1.Process(hp.Process(v)));
    }
    double e = 0;
    for (double v : y) e += v * v;
    const double scale = rms / std::sqrt(e / double(n));
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = float(y[i] * scale);
    return out;
}

struct Result {
    double t60 = 0, band[kBands] = {}, ned[2] = {}, rough[2] = {}, rho = 0, levelDb = 0;
    bool finite = true, silentAfterReset = true, repeatable = true, extremeFinite = true;
};

Result Measure(int type, double fs) {
    auto room = std::make_unique<maremba::RoomBank>();
    room->Allocate(fs);
    room->SetSampleRate(fs);
    room->SetType(type);
    Result res;
    float dl = 0, dr = 0;

    // Impulse response (and the same after Reset: bit-exact)
    const size_t n = size_t(5.0 * fs);
    std::vector<float> l(n), r(n);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) room->Reset();
        for (size_t i = 0; i < n; ++i) {
            float wl, wr;
            room->Process(i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, 1.0f, dl, dr, wl, wr);
            res.finite = res.finite && std::isfinite(wl) && std::isfinite(wr);
            if (pass) res.repeatable = res.repeatable && wl == l[i] && wr == r[i];
            else { l[i] = wl; r[i] = wr; }
        }
    }
    res.t60 = BroadbandT60(l, r, fs);
    for (int b = 0; b < kBands; ++b) res.band[b] = BandT60(l, r, fs, kTargets[type].bands[b].hz);
    res.ned[0] = EchoDensity(l, fs, 0.05, 0.15);
    res.ned[1] = EchoDensity(r, fs, 0.05, 0.15);
    res.rough[0] = Roughness(l, fs);
    res.rough[1] = Roughness(r, fs);
    res.rho = Correlation(l, r, fs);

    // Long-term wet level for a 1 s pink burst at -20 dBFS (3 s window)
    room->Reset();
    const auto burst = PinkBurst(fs, 1.0, 0.1);
    const size_t m = size_t(3.0 * fs);
    double energy = 0;
    for (size_t i = 0; i < m; ++i) {
        float wl, wr;
        const float x = i < burst.size() ? burst[i] : 0.0f;
        room->Process(x, x, 1.0f, dl, dr, wl, wr);
        res.finite = res.finite && std::isfinite(wl) && std::isfinite(wr);
        energy += 0.5 * (double(wl) * wl + double(wr) * wr);
    }
    res.levelDb = 10 * std::log10(energy / double(m));

    // Extreme finite input (full scale, huge, tiny) keeps every output finite
    const float extreme[] = { FLT_MAX, -FLT_MAX, 1.0e30f, -3.0e25f, 1.0e-40f, 1.0e5f, -1.0e5f, 0.5f };
    for (size_t i = 0; i < size_t(0.5 * fs); ++i) {
        float wl, wr;
        const float x = extreme[i % 8], y = extreme[(i * 5 + 3) % 8];
        room->Process(x, y, 1.0f, dl, dr, wl, wr);
        res.extremeFinite = res.extremeFinite && std::isfinite(wl) && std::isfinite(wr) && std::isfinite(dl) && std::isfinite(dr);
    }
    for (size_t i = 0; i < size_t(2.0 * fs); ++i) { // decay, including the silent-input tail
        float wl, wr;
        room->Process(0.0f, 0.0f, 1.0f, dl, dr, wl, wr);
        res.extremeFinite = res.extremeFinite && std::isfinite(wl) && std::isfinite(wr);
    }

    // Exact silence after Reset
    room->Reset();
    for (size_t i = 0; i < size_t(1.0 * fs); ++i) {
        float wl, wr;
        room->Process(0.0f, 0.0f, 1.0f, dl, dr, wl, wr);
        res.silentAfterReset = res.silentAfterReset && wl == 0.0f && wr == 0.0f && dl == 0.0f && dr == 0.0f;
    }
    return res;
}

} // namespace

int main() {
    std::printf("room     rate   | T60   (125   500    4k   10k) | NED L  NED R | rough L/R dB | rho    | level dB (before)\n");
    for (int type = 0; type < kRooms; ++type) {
        const Targets& t = kTargets[type];
        Result ref;
        for (int k = 0; k < kRatesCount; ++k) {
            const double fs = kRates[k];
            const Result res = Measure(type, fs);
            if (k == 0) ref = res;
            std::printf("%-8s %6.0f | %4.2f  (%4.2f  %4.2f  %4.2f  %4.2f) | %5.3f  %5.3f | %4.2f  %4.2f  | %+5.3f | %6.2f (%6.2f)\n",
                        kNames[type], fs, res.t60, res.band[0], res.band[1], res.band[2], res.band[3],
                        res.ned[0], res.ned[1], res.rough[0], res.rough[1], res.rho, res.levelDb, kPreChangeLevelDb[type][k]);
            const char* room = kNames[type];
            Check(std::abs(res.t60 / t.t60 - 1) <= t.t60Tolerance, room, fs, "T60 [s]", res.t60, t.t60);
            for (int b = 0; b < kBands; ++b) {
                char what[48];
                std::snprintf(what, sizeof what, "T60 at %.0f Hz [s]", t.bands[b].hz);
                Check(std::abs(res.band[b] / t.bands[b].seconds - 1) <= t.bands[b].tolerance, room, fs, what, res.band[b], t.bands[b].seconds);
                std::snprintf(what, sizeof what, "T60 at %.0f Hz vs 88.2 kHz", t.bands[b].hz);
                Check(std::abs(res.band[b] / ref.band[b] - 1) <= kRateT60Tolerance, room, fs, what, res.band[b] / ref.band[b], 1.0);
            }
            for (int c = 0; c < 2; ++c) {
                Check(res.ned[c] >= t.minEchoDensity, room, fs, c ? "NED right" : "NED left", res.ned[c], t.minEchoDensity);
                Check(res.rough[c] <= t.maxRoughnessDb, room, fs, c ? "roughness right [dB]" : "roughness left [dB]", res.rough[c], t.maxRoughnessDb);
            }
            Check(std::abs(res.rho) < kMaxCorrelation, room, fs, "L/R tail correlation", res.rho, kMaxCorrelation);
            Check(std::abs(res.levelDb - kPreChangeLevelDb[type][k]) <= kLevelToleranceDb, room, fs, "level vs before [dB]",
                  res.levelDb - kPreChangeLevelDb[type][k], kLevelToleranceDb);
            Check(std::abs(res.t60 / ref.t60 - 1) <= kRateT60Tolerance, room, fs, "T60 vs 88.2 kHz", res.t60 / ref.t60, 1.0);
            Check(std::abs(res.levelDb - ref.levelDb) <= kRateLevelToleranceDb, room, fs, "level vs 88.2 kHz [dB]",
                  res.levelDb - ref.levelDb, kRateLevelToleranceDb);
            Check(res.finite, room, fs, "finite output", 0, 1);
            Check(res.extremeFinite, room, fs, "finite output for extreme finite input", 0, 1);
            Check(res.repeatable, room, fs, "impulse response repeatable after Reset", 0, 1);
            Check(res.silentAfterReset, room, fs, "exact silence after Reset", 0, 1);
        }
    }
    if (gFailures) {
        std::printf(">>> %d ROOM QUALITY CHECK(S) FAILED <<<\n", gFailures);
        return 1;
    }
    std::printf("All room quality checks passed.\n");
    return 0;
}
