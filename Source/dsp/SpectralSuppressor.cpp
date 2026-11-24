// Unique Vintage Processor — dynamic resonance suppressor
#include "SpectralSuppressor.h"

namespace uvp
{
namespace
{
    // RIAA recording (pre-emphasis) power weighting, normalised to 1 kHz.
    // Pre = (1 + s*3180us)(1 + s*75us) / (1 + s*318us), with a 3.18us (50 kHz) limit pole.
    double riaaPrePower (double f)
    {
        auto mag2 = [] (double ff)
        {
            const double w = kTwoPi * ff;
            auto sq = [] (double v) { return v * v; };
            const double n1 = 1.0 + sq (w * 3180e-6), n2 = 1.0 + sq (w * 75e-6);
            const double d1 = 1.0 + sq (w * 318e-6), d2 = 1.0 + sq (w * 3.18e-6);
            return n1 * n2 / (d1 * d2);
        };
        return mag2 (std::max (f, 10.0)) / mag2 (1000.0);
    }

    struct AmountCurve
    {
        float depthMax, theta, slopeS, threshL, slopeL;
    };

    AmountCurve curveFor (float a)
    {
        AmountCurve c {};
        a = std::max (0.0f, std::min (11.0f, a));
        if (a <= 8.0f)
        {
            const float t = a / 8.0f;
            c.depthMax = 18.0f * std::pow (t, 1.1f);
            c.theta = 3.0f - 2.5f * t;
            // from a gentle ratio towards slight over-suppression at 8 (resonance carved just
            // below its neighbourhood: heavy, but still tonal)
            c.slopeS = 0.55f + 0.55f * t * std::sqrt (t);
            c.threshL = 2.0f - 20.0f * t;
            const float rl = 1.0f + 1.6f * t;
            c.slopeL = 1.0f - 1.0f / rl;
        }
        else
        {
            // 8..11: special-effect territory — deep, fast, almost limiting.
            const float t = (a - 8.0f) / 3.0f;
            c.depthMax = 18.0f + 12.0f * t;
            c.theta = 0.5f - 0.3f * t;
            c.slopeS = 1.1f + 0.5f * t;
            c.threshL = -18.0f - 16.0f * t;
            const float rl = 2.6f + 9.4f * t;
            c.slopeL = 1.0f - 1.0f / rl;
        }
        return c;
    }
} // namespace

void SpectralSuppressor::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    numCh = std::max (1, std::min (2, numChannels));

    // ~21 ms analysis window at any sample rate (1024 @ 44.1/48 kHz)
    int rateFactor = 1;
    while (fs / rateFactor > 64000.0) rateFactor <<= 1;
    N = 1024 * rateFactor;
    order = 0;
    while ((1 << order) < N) ++order;
    H = N / 8;
    K = N / 2 + 1;
    binHz = fs / N;
    fft = std::make_unique<juce::dsp::FFT> (order);

    window.resize ((size_t) N);
    double wsum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / N)); // periodic Hann
        wsum += window[(size_t) i];
    }
    // overlap of Hann^2 at hop N/8 sums to 3
    outScale = 1.0f / 3.0f;
    // a full-scale sine should give a peak-bin power of ~1 (0 dB)
    detScale = (float) (2.0 / wsum);

    inFifo.assign ((size_t) numCh, std::vector<float> ((size_t) N, 0.0f));
    outAccum.assign ((size_t) numCh, std::vector<float> ((size_t) N, 0.0f));
    spec.assign ((size_t) numCh, std::vector<float> ((size_t) (2 * N), 0.0f));

    auto zeros = [this] (std::vector<float>& v) { v.assign ((size_t) K, 0.0f); };
    for (auto* v : { &power, &tilt, &riaaW, &mask, &scratchA, &scratchB, &bandLvl, &refLvl, &loudLvl,
                     &energyScale, &grTarget, &grSmooth, &gains, &dispPow })
        zeros (*v);
    dispInDb.assign ((size_t) K, -120.0f);
    hBand.assign ((size_t) K, 0);
    hRef.assign ((size_t) K, 0);
    hDet.assign ((size_t) K, 0);
    scratchSq.assign ((size_t) K, 0.0f);
    spread.assign ((size_t) K, 0.0f);
    prefix.assign ((size_t) K + 1, 0.0);

    for (int k = 0; k < K; ++k)
    {
        const double f = std::max (1.0, k * binHz);
        tilt[(size_t) k] = (float) (std::max (f, 20.0) / 1000.0); // +3 dB/oct detector tilt
        riaaW[(size_t) k] = (float) riaaPrePower (f);
        gains[(size_t) k] = 1.0f;
    }
    kMin = std::max (1, (int) std::ceil (15.0 / binHz));

    const double hopSec = (double) H / fs;
    attackCoef = (float) (1.0 - std::exp (-hopSec / 0.001));  // ~1 ms attack
    releaseCoef = (float) (1.0 - std::exp (-hopSec / 0.030)); // ~30 ms release
    dispCoef = (float) (1.0 - std::exp (-hopSec / 0.060));    // display smoothing
    detCoef = (float) (1.0 - std::exp (-hopSec / 0.005));     // detector smoothing

    wetRamp.reset (fs, 0.03, 0.0f);
    lastQ = -1.0f;
    lastHpf = -1.0f;
    lastLpf = -1.0f;
    reset();
}

void SpectralSuppressor::reset()
{
    for (auto& v : inFifo) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : outAccum) std::fill (v.begin(), v.end(), 0.0f);
    std::fill (grSmooth.begin(), grSmooth.end(), 0.0f);
    std::fill (grTarget.begin(), grTarget.end(), 0.0f);
    std::fill (gains.begin(), gains.end(), 1.0f);
    std::fill (dispPow.begin(), dispPow.end(), 0.0f);
    std::fill (power.begin(), power.end(), 0.0f);
    std::fill (dispInDb.begin(), dispInDb.end(), -120.0f);
    fifoPos = 0;
    hopCount = 0;
    wetRamp.snap (params.amount > 0.0f ? 1.0f : 0.0f);
    meterDb.store (0.0f);
}

void SpectralSuppressor::updateKernelTables (float q) noexcept
{
    auto halfWidthFor = [] (double sigma)
    {
        if (sigma < 0.6) return 0;
        const double w = std::sqrt (4.0 * sigma * sigma + 1.0); // per-pass box width (3 passes ~ Gaussian)
        return std::max (0, (int) std::lround ((w - 1.0) * 0.5));
    };
    for (int k = 0; k < K; ++k)
    {
        const double f = std::max (k * binHz, binHz);
        // fine, fixed-resolution resonance detection (about Q 30, never under one bin)
        const double sigDet = std::max (1.0, (f / 30.0) / 2.3548 / binHz);
        hDet[(size_t) k] = halfWidthFor (sigDet);
        // neighbourhood for the reference: ~3/4 octave
        hRef[(size_t) k] = halfWidthFor (std::max (3.0 * sigDet, (0.52 * f / 2.3548) / binHz));
        // the Q fader: width of each cut and of the loudness (HF limiter) bands
        const double sigBand = (f / std::max (0.05, (double) q)) / 2.3548 / binHz;
        hBand[(size_t) k] = halfWidthFor (sigBand);
        energyScale[(size_t) k] = (float) std::max (1.0, 2.5066 * sigBand);
    }
    lastQ = q;
}

void SpectralSuppressor::updateMask (const Params& p) noexcept
{
    // the FILTER band, with exactly the band split's shape: LR4 low-pass at the top edge
    // minus LR4 low-pass at the bottom edge (a disabled edge opens that side fully)
    for (int k = 0; k < K; ++k)
    {
        const double f = k * binHz;
        double m = 0.0;
        if (k >= kMin)
        {
            const double below = p.hpfOn ? 1.0 / (1.0 + std::pow (f / std::max (10.0, (double) p.hpfHz), 4.0)) : 0.0;
            const double upTo = p.lpfOn ? 1.0 / (1.0 + std::pow (f / std::max (10.0, (double) p.lpfHz), 4.0)) : 1.0;
            m = std::max (0.0, upTo - below);
        }
        mask[(size_t) k] = (float) m;
    }
    lastHpf = p.hpfHz;
    lastHpfOn = p.hpfOn;
    lastLpf = p.lpfHz;
    lastLpfOn = p.lpfOn;
}

void SpectralSuppressor::boxSmooth3 (const float* in, float* out, const int* hw) noexcept
{
    const float* src = in;
    float* dst = scratchA.data();
    for (int pass = 0; pass < 3; ++pass)
    {
        prefix[0] = 0.0;
        for (int k = 0; k < K; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) src[k];
        float* target = (pass == 2) ? out : (pass == 0 ? scratchA.data() : scratchB.data());
        dst = target;
        for (int k = 0; k < K; ++k)
        {
            const int h = hw[k];
            const int lo = std::max (0, k - h), hi = std::min (K, k + h + 1);
            dst[k] = (float) ((prefix[(size_t) hi] - prefix[(size_t) lo]) / (double) (hi - lo));
        }
        src = dst;
    }
}

void SpectralSuppressor::process (float* const* ch, int nCh, int numSamples) noexcept
{
    nCh = std::min (nCh, numCh);
    wetRamp.setTarget (params.amount > 0.0f ? 1.0f : 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const float wet = wetRamp.next();
        for (int c = 0; c < nCh; ++c)
        {
            auto& fifo = inFifo[(size_t) c];
            auto& acc = outAccum[(size_t) c];
            const float delayed = fifo[(size_t) fifoPos]; // x[t - N], bit-exact dry path
            fifo[(size_t) fifoPos] = ch[c][i];
            const float processed = acc[(size_t) fifoPos];
            acc[(size_t) fifoPos] = 0.0f;
            ch[c][i] = (wet >= 1.0f) ? processed : (wet <= 0.0f ? delayed : delayed + wet * (processed - delayed));
        }
        if (++fifoPos == N) fifoPos = 0;
        if (++hopCount == H)
        {
            hopCount = 0;
            processFrame();
        }
    }
    if (nCh < numCh) // keep unused channel state silent
        for (int c = nCh; c < numCh; ++c) std::fill (outAccum[(size_t) c].begin(), outAccum[(size_t) c].end(), 0.0f);
}

void SpectralSuppressor::processFrame() noexcept
{
    const float amount = params.amount;
    if (std::abs (params.q - lastQ) > 0.002f * std::max (1.0f, params.q)) updateKernelTables (params.q);
    if (params.hpfHz != lastHpf || params.hpfOn != lastHpfOn || params.lpfHz != lastLpf || params.lpfOn != lastLpfOn)
        updateMask (params);

    // analysis ------------------------------------------------------------------
    for (int c = 0; c < numCh; ++c)
    {
        auto& s = spec[(size_t) c];
        const auto& fifo = inFifo[(size_t) c];
        const int first = N - fifoPos;
        for (int j = 0; j < first; ++j) s[(size_t) j] = fifo[(size_t) (fifoPos + j)] * window[(size_t) j];
        for (int j = first; j < N; ++j) s[(size_t) j] = fifo[(size_t) (j - first)] * window[(size_t) j];
        std::fill (s.begin() + N, s.end(), 0.0f);
        fft->performRealOnlyForwardTransform (s.data(), true);
    }
    const float ds2 = detScale * detScale;
    const float chNorm = 1.0f / (float) numCh;
    for (int k = 0; k < K; ++k)
    {
        float p = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float re = spec[(size_t) c][(size_t) (2 * k)], im = spec[(size_t) c][(size_t) (2 * k + 1)];
            p += re * re + im * im;
        }
        const float pk = p * ds2 * chNorm;
        power[(size_t) k] += detCoef * (pk - power[(size_t) k]); // ~5 ms detector smoothing
        dispPow[(size_t) k] += dispCoef * (pk - dispPow[(size_t) k]);
        dispInDb[(size_t) k] = powToDb (dispPow[(size_t) k], -140.0f);
    }

    const bool active = amount > 0.0f || wetRamp.getCurrent() > 0.0f;
    if (! active)
    {
        // relax the gain memory so re-engaging starts clean
        for (int k = 0; k < K; ++k) grSmooth[(size_t) k] *= (1.0f - releaseCoef);
        meterDb.store (0.0f, std::memory_order_relaxed);
        return;
    }

    computeGains (amount);

    // synthesis -------------------------------------------------------------------
    for (int c = 0; c < numCh; ++c)
    {
        auto& s = spec[(size_t) c];
        for (int k = 0; k < K; ++k)
        {
            s[(size_t) (2 * k)] *= gains[(size_t) k];
            s[(size_t) (2 * k + 1)] *= gains[(size_t) k];
        }
        fft->performRealOnlyInverseTransform (s.data());
        auto& acc = outAccum[(size_t) c];
        const int first = N - fifoPos;
        for (int j = 0; j < first; ++j) acc[(size_t) (fifoPos + j)] += s[(size_t) j] * window[(size_t) j] * outScale;
        for (int j = first; j < N; ++j) acc[(size_t) (j - first)] += s[(size_t) j] * window[(size_t) j] * outScale;
    }
}

void SpectralSuppressor::computeGains (float amount) noexcept
{
    const auto cv = curveFor (amount);
    const float fade = smoothstep (0.0f, 0.3f, amount);

    // 1) resonance detection at fine resolution on the tilted spectrum; the neighbourhood is a
    //    log-domain (geometric) mean so a strong resonance cannot inflate its own reference.
    std::vector<float>& tmp = grTarget; // reuse as temporary storage
    for (int k = 0; k < K; ++k) tmp[(size_t) k] = power[(size_t) k] * tilt[(size_t) k];
    boxSmooth3 (tmp.data(), bandLvl.data(), hDet.data());
    for (int k = 0; k < K; ++k) tmp[(size_t) k] = std::log (tmp[(size_t) k] + 1.0e-24f);
    boxSmooth3 (tmp.data(), refLvl.data(), hRef.data());
    // 2) loudness as the cutter sees it (RIAA velocity weighting), over bands as wide as the Q fader
    for (int k = 0; k < K; ++k) tmp[(size_t) k] = power[(size_t) k] * riaaW[(size_t) k];
    boxSmooth3 (tmp.data(), loudLvl.data(), hBand.data());

    constexpr float floorDb = -78.0f;
    // spike reduction per bin (squared, so the Q-wide spreading below is a quadratic mean)
    for (int k = 0; k < K; ++k)
    {
        float grS = 0.0f;
        if (mask[(size_t) k] > 0.0005f)
        {
            // 4.343 = 10/ln(10); 2.0 dB removes the periodogram's log-mean bias on noise
            const float S = powToDb (bandLvl[(size_t) k] + 1.0e-20f) - 4.3429448f * refLvl[(size_t) k] - 2.0f;
            const float L = powToDb (loudLvl[(size_t) k] * energyScale[(size_t) k] + 1.0e-20f);
            const float lw = smoothstep (floorDb, floorDb + 18.0f, L) * (1.0f + 0.5f * clamp01 ((L - cv.threshL) / 12.0f));
            grS = std::min (cv.depthMax, softKnee (S - cv.theta, 4.0f) * cv.slopeS * lw);
        }
        scratchSq[(size_t) k] = grS * grS;
    }
    // 3) shape each cut to the chosen Q: narrow = the detected notch, broad = a wide gentle dip
    boxSmooth3 (scratchSq.data(), spread.data(), hBand.data());

    const float narrowness = smoothstep (0.4055f, 2.4849f, std::log (std::max (0.05f, params.q))); // Q 1.5 -> 12
    float peak = 0.0f;
    for (int k = 0; k < K; ++k)
    {
        float target = 0.0f;
        const float m = mask[(size_t) k];
        if (m > 0.0005f)
        {
            const float L = powToDb (loudLvl[(size_t) k] * energyScale[(size_t) k] + 1.0e-20f);
            const float grL = softKnee (L - cv.threshL, 6.0f) * cv.slopeL;
            // broad Q: the quadratic-mean spread (wide, gentle dips); narrow Q: keep each notch's depth
            const float spreadGr = std::sqrt (std::max (0.0f, spread[(size_t) k]));
            const float notchGr = std::sqrt (std::max (0.0f, scratchSq[(size_t) k]));
            const float grS = spreadGr + narrowness * (std::max (spreadGr, notchGr) - spreadGr);
            target = std::min (cv.depthMax, grS + grL) * m * fade;
        }
        // fixed fast ballistics, per bin, in dB
        float& g = grSmooth[(size_t) k];
        g += (target > g ? attackCoef : releaseCoef) * (target - g);
        gains[(size_t) k] = std::exp (-0.11512925f * g); // 10^(-g/20)
        peak = std::max (peak, g);
        tmp[(size_t) k] = target;
    }
    meterDb.store (peak, std::memory_order_relaxed);
}

} // namespace uvp
