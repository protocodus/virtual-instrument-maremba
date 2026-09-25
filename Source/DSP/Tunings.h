#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maremba {

// Traditional tunings. The scale's degrees sit on the keys a player would
// reach for (heptatonic scales on the white keys, pentatonic ones on C D E G
// A); the remaining keys fall between their neighbours so chromatic runs stay
// monotonic.
//
// * Bala 7-equal: Malinke/Mandinka balafons measured equiheptatonic, 171.4
//   cents per step (Rouget & Schwarz 1969: three 18-key instruments, steps
//   172 cents on average; Jessup 1983, "not as accurate as some have
//   stated"). Their keys sit 5-7 cents (SD) off the equal grid.
// * Timbila: H. Tracey's Chopi timbila of the Zavala orchestra, 252 276 304
//   336 368 408 456 504 Hz (157-193 cent steps). Makers tune the octave, then
//   copy it: "as a rule their octaves are true and exact" (Tracey 1948).
// * Pentatonic 5-equal: 240-cent steps, the equipentatonic tuning of the
//   Kiganda amadinda and akadinda (Kubik 1969; Cooke 1992: measured keys
//   stray "from a few cents to more than 60 cents"; the Senufo balafon is
//   equipentatonic too, Zemp & Soro 2010). Keys stray 25 cents (SD).
// * Sambla: degrees 1, flat 3 (just under 300 c), 3 (just over 100 c above it),
//   5 and 6 (McPherson 2018), on C, D#, E, G, A.
// * Gyil: Godsey's averaged Birifor kogyil (1980, reproduced by Hogan 2015),
//   fourteen keys from 102.75 Hz, about 238-cent steps and slightly narrow
//   octaves (1169-1205 c), key 1 on C3. An individual gyil strays about 30
//   cents more (Wiggins 2011: steps 100-320 c, SD 35).
// * Valimba: the Sena xylophone A. Tracey measured key by key (1991): 170-cent
//   steps, uneven octaves (1111-1273 c) and flat top keys; key 4 (128 Hz) on C3.
// Measured keyboards continue beyond their range in exact octaves.
enum class ETuningScale : int {
    Equal = 0,
    BalaSevenEqual = 1,
    ChopiTimbila = 2,
    PentatonicFiveEqual = 3,
    Sambla = 4,
    Gyil = 5,
    Valimba = 6,
    Count = 7
};

// Absolute cents of each pitch class above C: the scale's own degrees, before
// any key's own deviation (the measured keyboards list their middle octave).
inline const double* TuningPitchClassCents(int scale) {
    static constexpr double kEqual[12] = {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100};
    //                                      C      C#     D      D#     E      F      F#     G      G#     A      A#      B
    static constexpr double kBala[12] = {0.0, 85.7, 171.4, 257.1, 342.9, 514.3, 600.0, 685.7, 771.4, 857.1, 942.9, 1028.6};
    static constexpr double kTimbila[12] = {0.0, 78.8, 157.5, 241.2, 324.8, 498.0, 576.8, 655.6, 745.0, 834.4, 930.7, 1026.9};
    static constexpr double kFive[12] = {0.0, 120.0, 240.0, 360.0, 480.0, 600.0, 660.0, 720.0, 840.0, 960.0, 1020.0, 1080.0};
    static constexpr double kSambla[12] = {0.0, 100.0, 200.0, 295.0, 400.0, 550.0, 625.0, 700.0, 800.0, 900.0, 1000.0, 1100.0};
    // Gyil keys 6-10 (C4..A4) re key 6, and Valimba keys 11-17 (C4..B4) re key 11.
    static constexpr double kGyil[12] = {0.0, 106.5, 213.1, 338.5, 464.0, 541.0, 618.0, 695.0, 817.5, 940.0, 1019.7, 1099.4};
    static constexpr double kValimba[12] = {0.0, 86.3, 172.7, 264.4, 356.2, 498.0, 572.6, 647.1, 715.7, 784.3, 899.8, 1015.4};
    switch (std::clamp(scale, 0, static_cast<int>(ETuningScale::Count) - 1)) {
        case 1: return kBala;
        case 2: return kTimbila;
        case 3: return kFive;
        case 4: return kSambla;
        case 5: return kGyil;
        case 6: return kValimba;
        default: return kEqual;
    }
}

namespace detail {

// The keys carrying a scale's degrees (heptatonic scales on the white keys,
// pentatonic ones on C D E G A, Sambla on C D# E G A); the others fall
// between them.
inline bool IsDegreeKey(int scale, int noteNumber) {
    const int pc = ((noteNumber % 12) + 12) % 12;
    static constexpr bool kAll[12] = {true, true, true, true, true, true, true, true, true, true, true, true};
    static constexpr bool kWhite[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
    static constexpr bool kFive[12] = {true, false, true, false, true, false, false, true, false, true, false, false};
    static constexpr bool kSambla[12] = {true, false, false, true, true, false, false, true, false, true, false, false};
    switch (scale) {
        case 1: case 2: case 6: return kWhite[pc];
        case 3: case 5: return kFive[pc];
        case 4: return kSambla[pc];
        default: return kAll[pc];
    }
}

// A measured keyboard: each key's cents above the first, which sits on MIDI
// note `firstNote`; the keys follow one another on the scale's degree keys.
struct MeasuredKeys {
    int firstNote;
    int count;
    const double* cents;
};

inline const MeasuredKeys* MeasuredKeyboard(int scale) {
    // Godsey 1980, averaged kogyil: 102.75 116.88 134.26 154.67 179.42 204.79
    // 231.61 267.74 305.96 352.47 404.65 463.03 537.03 611.92 Hz, on C3 D3 E3
    // G3 A3 C4 ... G5.
    static constexpr double kGyilCents[14] = {0.0, 223.1, 463.1, 708.1, 965.0, 1194.0, 1407.1, 1658.0, 1889.0, 2134.0,
                                              2373.0, 2606.4, 2863.0, 3089.0};
    // A. Tracey 1991, Sena valimba keys 4-24: 128 146 162 178 194 220 240 267
    // 295 328 356 388 420 480 524 584 640 712 776 864 912 Hz, on C3..B5. (Key
    // 2, 89 Hz, is exactly an octave below key 7; key 3 is the unpitched
    // rattle key.)
    static constexpr double kValimbaCents[21] = {
        0.0, 227.8, 407.8, 570.9, 719.9, 937.6, 1088.3, 1272.8, 1445.5, 1629.1, 1770.9,
        1919.9, 2057.1, 2288.3, 2440.1, 2627.8, 2786.3, 2970.9, 3119.9, 3305.9, 3399.5};
    static constexpr MeasuredKeys kGyil{48, 14, kGyilCents};
    static constexpr MeasuredKeys kValimba{48, 21, kValimbaCents};
    if (scale == static_cast<int>(ETuningScale::Gyil)) return &kGyil;
    if (scale == static_cast<int>(ETuningScale::Valimba)) return &kValimba;
    return nullptr;
}

// Offset from equal temperament of a measured keyboard's degree key; beyond
// the measured range the nearest measured octave repeats exactly.
inline double MeasuredOffsetCents(int scale, const MeasuredKeys& keys, int note) {
    int last = keys.firstNote, index = 0;
    for (int n = keys.firstNote; index + 1 < keys.count; ++n)
        if (IsDegreeKey(scale, n + 1)) { last = n + 1; ++index; }
    // Whole octaves in or out leave the offset as it is.
    while (note > last) note -= 12;
    while (note < keys.firstNote) note += 12;
    int key = 0;
    for (int n = keys.firstNote; n < note; ++n)
        if (IsDegreeKey(scale, n + 1)) ++key;
    return keys.cents[std::min(key, keys.count - 1)] - 100.0 * (note - keys.firstNote);
}

// A fixed deviation per key, about normally distributed with unit SD (the
// sum of three uniform draws, scaled), the same on every strike, reset and rate.
inline double KeyScatter(int scale, int noteNumber) {
    uint32_t state = static_cast<uint32_t>(noteNumber & 127) * 0x9E3779B1u ^ static_cast<uint32_t>(scale + 11) * 0xC2B2AE35u;
    double sum = 0.0;
    for (int k = 0; k < 3; ++k) {
        state += 0x9E3779B9u;
        uint32_t z = state;
        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        z ^= z >> 15;
        sum += static_cast<double>(z >> 8) * (2.0 / 16777216.0) - 1.0;
    }
    return sum;   // three uniforms on [-1, 1): SD 1
}

// SD of the per-key deviation (cents) of each tradition's hand-tuned keys;
// the Timbila copies its octave exactly and the Valimba lists its own keys.
inline double KeyScatterCents(int scale) {
    switch (scale) {
        case 1: return 6.0;    // Rouget & Schwarz's balafons
        case 3: return 25.0;   // amadinda keys (Cooke 1992)
        case 5: return 30.0;   // an individual gyil against Godsey's average
        default: return 0.0;
    }
}

inline double DegreeOffsetCents(int scale, int noteNumber) {
    const double scatter = KeyScatterCents(scale) * KeyScatter(scale, noteNumber);
    if (const MeasuredKeys* keys = MeasuredKeyboard(scale))
        return MeasuredOffsetCents(scale, *keys, noteNumber) + scatter;
    const int pc = ((noteNumber % 12) + 12) % 12;
    return TuningPitchClassCents(scale)[pc] - 100.0 * pc + scatter;
}

} // namespace detail

// Offset of a MIDI note from equal temperament under the given scale: a
// degree key's own tuning, or halfway along between the degree keys either
// side (so chromatic runs rise).
inline double TuningOffsetCents(int scale, int noteNumber) {
    scale = std::clamp(scale, 0, static_cast<int>(ETuningScale::Count) - 1);
    if (detail::IsDegreeKey(scale, noteNumber)) return detail::DegreeOffsetCents(scale, noteNumber);
    int lo = noteNumber - 1, hi = noteNumber + 1;
    while (!detail::IsDegreeKey(scale, lo)) --lo;
    while (!detail::IsDegreeKey(scale, hi)) ++hi;
    const double a = 100.0 * lo + detail::DegreeOffsetCents(scale, lo);
    const double b = 100.0 * hi + detail::DegreeOffsetCents(scale, hi);
    return a + (b - a) * (noteNumber - lo) / static_cast<double>(hi - lo) - 100.0 * noteNumber;
}

} // namespace maremba
