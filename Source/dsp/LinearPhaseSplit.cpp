// Unique Vintage Processor — linear-phase band split
#include "LinearPhaseSplit.h"

namespace uvp
{

void LinearPhaseSplit::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    numCh = std::max (1, std::min (2, numChannels));
    int rateFactor = 1;
    while (fs / rateFactor > 64000.0) rateFactor <<= 1;
    L = 4096 * rateFactor;  // ~85 ms of filter at any rate
    B = 256 * rateFactor;
    F = 2 * B;
    P = L / B;

    int orderB = 0; while ((1 << orderB) < F) ++orderB;
    int orderL = 0; while ((1 << orderL) < L) ++orderL;
    fftBlock = std::make_unique<juce::dsp::FFT> (orderB);
    fftDesign = std::make_unique<juce::dsp::FFT> (orderL);

    designBuf.assign ((size_t) (2 * L), 0.0f);
    designTmp.assign ((size_t) L, 0.0f);
    scratch.assign ((size_t) (2 * F), 0.0f);
    accA.assign ((size_t) (2 * F), 0.0f);
    accB.assign ((size_t) (2 * F), 0.0f);
    window.resize ((size_t) L);
    for (int n = 0; n < L; ++n) // periodic Blackman: w[0] = 0, symmetric about L/2
        window[(size_t) n] = (float) (0.42 - 0.5 * std::cos (kTwoPi * n / L) + 0.08 * std::cos (2.0 * kTwoPi * n / L));
    for (auto& f : filt) f.assign ((size_t) (P * 2 * F), 0.0f);

    fdl.assign ((size_t) numCh, std::vector<float> ((size_t) (P * 2 * F), 0.0f));
    inBuf.assign ((size_t) numCh, std::vector<float> ((size_t) B, 0.0f));
    prevBlock.assign ((size_t) numCh, std::vector<float> ((size_t) B, 0.0f));
    lpOut.assign ((size_t) numCh, std::vector<float> ((size_t) B, 0.0f));
    dryDelay.resize ((size_t) numCh);
    for (auto& d : dryDelay) { d.prepare (B + L / 2 + 4); d.setDelay (B + L / 2); }

    enableRamp.reset (fs, 0.02, targetEnabled ? 1.0f : 0.0f);
    curBank = 0;
    designInto (0, std::max (20.0f, std::min (15000.0f, targetHz)));
    designedHz = std::max (20.0f, std::min (15000.0f, targetHz));
    crossfadePending = false;
    reset();
}

void LinearPhaseSplit::reset()
{
    for (auto& v : fdl) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : inBuf) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : prevBlock) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : lpOut) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& d : dryDelay) d.reset();
    fdlPos = 0;
    pos = 0;
    enableRamp.snap (targetEnabled ? 1.0f : 0.0f);
}

void LinearPhaseSplit::designInto (int bank, double fc) noexcept
{
    // zero-phase LR4 low-pass magnitude, sampled on an L-point grid
    std::fill (designBuf.begin(), designBuf.end(), 0.0f);
    for (int k = 0; k <= L / 2; ++k)
    {
        const double f = k * fs / L;
        const double r = f / fc;
        const double r4 = (r * r) * (r * r);
        designBuf[(size_t) (2 * k)] = (float) (1.0 / (1.0 + r4));
    }
    fftDesign->performRealOnlyInverseTransform (designBuf.data()); // zero-phase, circular
    // centre at L/2, window, normalise DC
    double sum = 0.0;
    auto& tmp = designTmp;
    for (int n = 0; n < L; ++n)
    {
        const int src = (n - L / 2 + L) % L;
        tmp[(size_t) n] = designBuf[(size_t) src] * window[(size_t) n];
        sum += tmp[(size_t) n];
    }
    const float norm = (float) (1.0 / sum);
    auto& dst = filt[bank];
    for (int p = 0; p < P; ++p)
    {
        std::fill (scratch.begin(), scratch.end(), 0.0f);
        for (int n = 0; n < B; ++n) scratch[(size_t) n] = tmp[(size_t) (p * B + n)] * norm;
        fftBlock->performRealOnlyForwardTransform (scratch.data(), true);
        std::copy (scratch.begin(), scratch.begin() + 2 * F, dst.begin() + (size_t) (p * 2 * F));
    }
}

void LinearPhaseSplit::processBlock() noexcept
{
    // new design at block boundaries only; crossfade old -> new over one block
    const double want = std::max (20.0f, std::min (15000.0f, targetHz));
    bool doFade = false;
    if (! crossfadePending && std::abs (want - designedHz) > designedHz * 0.0025)
    {
        designInto (1 - curBank, want);
        designedHz = want;
        crossfadePending = true;
    }
    if (crossfadePending) { doFade = true; crossfadePending = false; }

    fdlPos = (fdlPos + 1) % P;
    const int nb = B + 1;
    for (int c = 0; c < numCh; ++c)
    {
        auto& slot = fdl[(size_t) c];
        float* X = slot.data() + (size_t) (fdlPos * 2 * F);
        std::fill (X, X + 2 * F, 0.0f);
        std::copy (prevBlock[(size_t) c].begin(), prevBlock[(size_t) c].end(), X);
        std::copy (inBuf[(size_t) c].begin(), inBuf[(size_t) c].end(), X + B);
        fftBlock->performRealOnlyForwardTransform (X, true);

        auto accumulate = [&] (const std::vector<float>& Hbank, std::vector<float>& acc)
        {
            std::fill (acc.begin(), acc.end(), 0.0f);
            for (int p = 0; p < P; ++p)
            {
                const float* Xp = slot.data() + (size_t) (((fdlPos - p + P) % P) * 2 * F);
                const float* Hp = Hbank.data() + (size_t) (p * 2 * F);
                for (int k = 0; k < nb; ++k)
                {
                    const float xr = Xp[2 * k], xi = Xp[2 * k + 1], hr = Hp[2 * k], hi = Hp[2 * k + 1];
                    acc[(size_t) (2 * k)] += xr * hr - xi * hi;
                    acc[(size_t) (2 * k + 1)] += xr * hi + xi * hr;
                }
            }
            fftBlock->performRealOnlyInverseTransform (acc.data());
        };

        auto& out = lpOut[(size_t) c];
        if (doFade)
        {
            accumulate (filt[curBank], accA);
            accumulate (filt[1 - curBank], accB);
            for (int n = 0; n < B; ++n)
            {
                const float w = (float) (n + 1) / (float) B;
                out[(size_t) n] = accA[(size_t) (B + n)] + w * (accB[(size_t) (B + n)] - accA[(size_t) (B + n)]);
            }
        }
        else
        {
            accumulate (filt[curBank], accA);
            for (int n = 0; n < B; ++n) out[(size_t) n] = accA[(size_t) (B + n)];
        }
        std::copy (inBuf[(size_t) c].begin(), inBuf[(size_t) c].end(), prevBlock[(size_t) c].begin());
    }
    if (doFade) curBank = 1 - curBank;
}

void LinearPhaseSplit::process (const float* const* in, float* const* low, float* const* high, int nCh, int numSamples) noexcept
{
    nCh = std::min (nCh, numCh);
    enableRamp.setTarget (targetEnabled ? 1.0f : 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = enableRamp.next();
        for (int c = 0; c < nCh; ++c)
        {
            inBuf[(size_t) c][(size_t) pos] = in[c][i];
            const float lp = lpOut[(size_t) c][(size_t) pos] * m;
            const float xd = dryDelay[(size_t) c].process (in[c][i]);
            low[c][i] = lp;
            high[c][i] = xd - lp;
        }
        if (++pos == B)
        {
            pos = 0;
            processBlock();
        }
    }
}

} // namespace uvp
