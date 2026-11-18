// Small DSP helpers for Dust Mote. Plain scalar C++ (no SIMD intrinsics, no -march).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dm
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }

// xorshift32: cheap, deterministic, allocation-free random numbers for the audio thread.
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) { s = v ? v : 0x9e3779b9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bi() { return uni() * 2.0f - 1.0f; }                              // [-1, 1)
};

// One-pole parameter smoother.
struct Smoother
{
    float value = 0, target = 0, coef = 1;
    void setTime (double sr, double ms) { coef = ms <= 0 ? 1.0f : (float) (1.0 - std::exp (-1.0 / (sr * ms * 0.001))); }
    void reset (float v) { value = target = v; }
    float next() { value += coef * (target - value); return value; }
};

// Linear ramp that reaches its target in a fixed time (used for the Freeze write gain).
struct Ramp
{
    float value = 1, target = 1, step = 1;
    void set (float t, double sr, double ms)
    {
        target = t;
        const double n = std::max (1.0, sr * ms * 0.001);
        step = (float) (1.0 / n);
    }
    float next()
    {
        if (value < target) value = std::min (target, value + step);
        else if (value > target) value = std::max (target, value - step);
        return value;
    }
};

struct OnePoleLP
{
    float z = 0, a = 1;
    void setCutoff (double sr, double hz) { a = (float) (1.0 - std::exp (-2.0 * kPi * hz / sr)); }
    float process (float x) { z += a * (x - z); return z; }
    void reset() { z = 0; }
};

// Linear below the knee, smoothly saturating above it. Keeps feedback and overdub bounded.
inline float softLimit (float x, float knee = 0.9f, float ceiling = 1.6f)
{
    const float ax = std::abs (x);
    if (ax <= knee) return x;
    const float room = ceiling - knee;
    const float y = knee + room * std::tanh ((ax - knee) / room);
    return x < 0 ? -y : y;
}

// 4-point, 3rd-order Hermite interpolation.
inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

// Raised-cosine half table, used to build Hann (alpha 1) to Tukey (small alpha) grain windows.
class WindowTable
{
public:
    static constexpr int kSize = 1024;
    WindowTable()
    {
        for (int i = 0; i <= kSize; ++i)
            rise[(size_t) i] = 0.5f - 0.5f * (float) std::cos (kPi * i / kSize);
    }
    // t in [0, 1] across the grain; alpha in (0, 1] is the tapered fraction (1 = Hann).
    float at (float t, float alpha) const
    {
        const float half = alpha * 0.5f;
        float u;
        if (t < half) u = t / half;
        else if (t > 1.0f - half) u = (1.0f - t) / half;
        else return 1.0f;
        u = std::clamp (u, 0.0f, 1.0f) * kSize;
        const int i = std::min ((int) u, kSize - 1);
        const float f = u - (float) i;
        return rise[(size_t) i] + f * (rise[(size_t) i + 1] - rise[(size_t) i]);
    }

private:
    std::array<float, kSize + 1> rise {};
};
} // namespace dm
