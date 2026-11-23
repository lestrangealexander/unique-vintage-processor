// Unique Vintage Processor — shared DSP utilities
// Sound Aquarium
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace uvp
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline double dbToGainD (double db) noexcept { return std::pow (10.0, db * 0.05); }
inline float gainToDb (float g, float floorDb = -140.0f) noexcept
{
    return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb;
}
inline float powToDb (float p, float floorDb = -160.0f) noexcept
{
    return p > 1.0e-30f ? std::max (floorDb, 10.0f * std::log10 (p)) : floorDb;
}
template <typename T> inline T clamp01 (T v) noexcept { return std::min (T (1), std::max (T (0), v)); }
template <typename T> inline T smoothstep (T e0, T e1, T x) noexcept
{
    auto t = clamp01 ((x - e0) / (e1 - e0));
    return t * t * (T (3) - T (2) * t);
}
/** Quadratic soft knee: 0 below -knee/2, x above +knee/2, smooth in between. */
inline float softKnee (float x, float knee) noexcept
{
    if (knee <= 0.0f) return std::max (0.0f, x);
    const float h = 0.5f * knee;
    if (x <= -h) return 0.0f;
    if (x >= h) return x;
    const float t = x + h;
    return t * t / (2.0f * knee);
}

//==============================================================================
/** Per-sample linear ramp towards a target (zipper-free parameter changes). */
class LinearRamp
{
public:
    void reset (double sampleRate, double rampSeconds, float initial) noexcept
    {
        steps = std::max (1, (int) std::lround (sampleRate * rampSeconds));
        current = target = initial;
        remaining = 0;
    }
    void setTarget (float t) noexcept
    {
        if (t == target) return;
        target = t;
        remaining = steps;
        delta = (target - current) / (float) steps;
    }
    void snap (float v) noexcept { current = target = v; remaining = 0; }
    inline float next() noexcept
    {
        if (remaining > 0)
        {
            current += delta;
            if (--remaining == 0) current = target;
        }
        return current;
    }
    bool isRamping() const noexcept { return remaining > 0; }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, delta = 0.0f;
    int steps = 1, remaining = 0;
};

//==============================================================================
/** Biquad in transposed direct form II, double precision state. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void setCoefs (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    // RBJ cookbook designs ----------------------------------------------------
    void setPeak (double fs, double f0, double q, double gainDb) noexcept
    {
        f0 = std::min (f0, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * f0 / fs;
        const double al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setCoefs (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowPass (double fs, double f0, double q) noexcept
    {
        f0 = std::min (f0, fs * 0.49);
        const double w = kTwoPi * f0 / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setCoefs ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighPass (double fs, double f0, double q) noexcept
    {
        f0 = std::min (f0, fs * 0.49);
        const double w = kTwoPi * f0 / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setCoefs ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighShelf (double fs, double f0, double q, double gainDb) noexcept
    {
        f0 = std::min (f0, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * f0 / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q), sA = 2 * std::sqrt (A) * al;
        setCoefs (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                  (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void setLowShelf (double fs, double f0, double q, double gainDb) noexcept
    {
        f0 = std::min (f0, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0), w = kTwoPi * f0 / fs;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q), sA = 2 * std::sqrt (A) * al;
        setCoefs (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                  (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    /** Magnitude response at frequency f (for tests/calibration). */
    double magnitude (double fs, double f) const noexcept
    {
        const double w = kTwoPi * f / fs;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

//==============================================================================
/** First-order section y = (b0 + b1 z^-1)/(1 + a1 z^-1). */
struct FirstOrder
{
    double b0 = 1, b1 = 0, a1 = 0, z = 0;
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
    void reset() noexcept { z = 0; }

    /** Analog H(s) = (1 + s/wz)/(1 + s/wp), bilinear with both corners pre-warped.
        Corners above 0.45*fs are clamped so the design stays well behaved. */
    void setZeroPole (double fs, double fz, double fp) noexcept
    {
        const double lim = 0.45 * fs;
        fz = std::min (fz, lim);
        fp = std::min (fp, lim * 0.999);
        const double T = 1.0 / fs, K = 2.0 / T;
        const double wz = K * std::tan (kPi * fz / fs);
        const double wp = K * std::tan (kPi * fp / fs);
        // H(s) = (wp/wz) * (s + wz)/(s + wp)
        const double g = wp / wz;
        const double nb0 = g * (K + wz), nb1 = g * (wz - K);
        const double na0 = K + wp, na1 = wp - K;
        b0 = nb0 / na0; b1 = nb1 / na0; a1 = na1 / na0;
    }
    /** Exact digital inverse of another section (used for de-emphasis). */
    void setInverseOf (const FirstOrder& o) noexcept
    {
        // (b0 + b1 z^-1)/(1 + a1 z^-1) -> (1 + a1 z^-1)/(b0 + b1 z^-1)
        b0 = 1.0 / o.b0; b1 = o.a1 / o.b0; a1 = o.b1 / o.b0;
    }
    void setHighPass (double fs, double fc) noexcept
    {
        const double K = std::tan (kPi * std::min (fc, 0.45 * fs) / fs);
        const double n = 1.0 / (1.0 + K);
        b0 = n; b1 = -n; a1 = (K - 1.0) * n;
    }
    void setLowPass (double fs, double fc) noexcept
    {
        const double K = std::tan (kPi * std::min (fc, 0.49 * fs) / fs);
        const double n = 1.0 / (1.0 + K);
        b0 = K * n; b1 = K * n; a1 = (K - 1.0) * n;
    }
    double magnitude (double fs, double f) const noexcept
    {
        const double w = kTwoPi * f / fs, cr = std::cos (w), ci = -std::sin (w);
        const double nr = b0 + b1 * cr, ni = b1 * ci, dr = 1 + a1 * cr, di = a1 * ci;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

//==============================================================================
/** Integer delay line (power-of-two ring). */
class IntDelay
{
public:
    void prepare (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 2) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }
    void setDelay (int d) noexcept { delay = std::max (0, std::min (d, mask - 1)); }
    int getDelay() const noexcept { return delay; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    inline float process (float x) noexcept
    {
        buf[(size_t) pos] = x;
        const float y = buf[(size_t) ((pos - delay) & mask)];
        pos = (pos + 1) & mask;
        return y;
    }
private:
    std::vector<float> buf;
    int mask = 0, pos = 0, delay = 0;
};

//==============================================================================
/** Fractional delay with 4-point cubic Hermite interpolation, modulatable per sample. */
class ModDelay
{
public:
    void prepare (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    inline void push (float x) noexcept
    {
        buf[(size_t) pos] = x;
        pos = (pos + 1) & mask;
    }
    /** Read `delay` samples behind the most recently pushed sample (delay >= 1). */
    inline float read (double delay) const noexcept
    {
        const double d = std::max (1.0, delay);
        const int di = (int) d;
        const float fr = (float) (d - di);
        const int base = pos - 1 - di;
        const float y0 = buf[(size_t) ((base + 1) & mask)];
        const float y1 = buf[(size_t) (base & mask)];
        const float y2 = buf[(size_t) ((base - 1) & mask)];
        const float y3 = buf[(size_t) ((base - 2) & mask)];
        // Hermite between y1 (t=0) and y2 (t=1)
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + c0;
    }
private:
    std::vector<float> buf;
    int mask = 0, pos = 0;
};

//==============================================================================
/** Small fast PRNG (xorshift) for modulation noise — deterministic per seed. */
struct XorShift
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) noexcept { s = v ? v : 0x9E3779B9u; }
    inline uint32_t nextU() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    /** Uniform in [-1, 1). */
    inline float nextBipolar() noexcept { return (float) ((double) nextU() * (2.0 / 4294967296.0) - 1.0); }
    inline float nextUnipolar() noexcept { return (float) ((double) nextU() * (1.0 / 4294967296.0)); }
};

//==============================================================================
/** Lock-free single-producer/single-consumer "latest value" mailbox for display data. */
template <typename T>
class TripleBuffer
{
public:
    T& beginWrite() noexcept { return bufs[writeIdx]; }
    void endWrite() noexcept
    {
        writeIdx = shared.exchange (writeIdx | kDirty, std::memory_order_acq_rel) & kMask;
    }
    /** Returns true if fresh data was swapped in; `out` then refers to it. */
    bool read (const T*& out) noexcept
    {
        if ((shared.load (std::memory_order_relaxed) & kDirty) == 0)
        {
            out = &bufs[readIdx];
            return false;
        }
        readIdx = shared.exchange (readIdx, std::memory_order_acq_rel) & kMask;
        out = &bufs[readIdx];
        return true;
    }
private:
    static constexpr int kDirty = 4, kMask = 3;
    T bufs[3];
    int writeIdx = 0, readIdx = 1;
    std::atomic<int> shared { 2 };
};

} // namespace uvp
