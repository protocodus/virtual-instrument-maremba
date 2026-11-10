#pragma once

// Two- and four-lane vectors for the per-frame loops that only an
// auto-vectorizing compiler ran wide. Reason's toolchain compiles Source/DSP
// without auto-vectorization, so there those loops ran one float at a time;
// GNU/Clang vector extensions survive it and lower to SSE2 and NEON.
//
// Every kernel written with them keeps the scalar code's operations, their
// order and their statement boundaries in each lane, and reorders nothing
// across lanes, so it is bit-identical to the scalar loop it replaces
// (Maremba.SimdKernel compares each with its reference). Other compilers
// take the scalar loops.
#if defined(__clang__) || defined(__GNUC__)
#define MAREMBA_SIMD 1
#else
#define MAREMBA_SIMD 0
#endif

namespace maremba {

#if MAREMBA_SIMD
// Attributed typedefs, not using-aliases: Clang drops the reduced alignment
// from an alias, and these are read from plain float and double arrays.
typedef float Float4 __attribute__((vector_size(16), aligned(4), may_alias));
typedef double Double2 __attribute__((vector_size(16), aligned(8), may_alias));
static_assert(alignof(Float4) == 4 && alignof(Double2) == 8, "lanes load from unaligned arrays");

inline Float4 LoadFloat4(const float* p) { return *reinterpret_cast<const Float4*>(p); }
inline void StoreFloat4(float* p, Float4 v) { *reinterpret_cast<Float4*>(p) = v; }
inline Float4 SplatFloat4(float x) { return Float4{x, x, x, x}; }
// Lane-wise `std::abs(x) < limit ? 0.0f : x`, the engine's flush of a value
// on its way to a persistent state: a small lane becomes +0, a NaN lane stays.
typedef int Int4 __attribute__((vector_size(16), aligned(4), may_alias));
inline Float4 FlushFloat4(Float4 x, float limit) {
    const Int4 bits = (Int4)x;
    const Float4 magnitude = (Float4)(bits & Int4{0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff});
    const Int4 small = magnitude < SplatFloat4(limit);
    return (Float4)(bits & ~small);
}
inline Double2 LoadDouble2(const double* p) { return *reinterpret_cast<const Double2*>(p); }
inline void StoreDouble2(double* p, Double2 v) { *reinterpret_cast<Double2*>(p) = v; }
inline Double2 SplatDouble2(double x) { return Double2{x, x}; }
// The first (or last) two floats of each vector, a's then b's: a pair of
// floats moves as one double lane, which needs no shuffle builtin.
inline Float4 LowPairs(Float4 a, Float4 b) { return (Float4)Double2{((Double2)a)[0], ((Double2)b)[0]}; }
inline Float4 HighPairs(Float4 a, Float4 b) { return (Float4)Double2{((Double2)a)[1], ((Double2)b)[1]}; }
#endif

} // namespace maremba
