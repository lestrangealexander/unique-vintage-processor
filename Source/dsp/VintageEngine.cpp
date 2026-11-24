// Unique Vintage Processor — complete signal chain
#include "VintageEngine.h"
#include "AutoGainTables.h"
#include "AutoGainLpfTables.h"

namespace uvp
{

float VintageEngine::tableCompDb (int mode, float amount, float hpfHz, bool hpfOn, float inputDb) noexcept
{
    using namespace agc;
    const auto& T = vintageTable();
    mode = std::max (0, std::min (2, mode));
    const float ai = std::max (0.0f, std::min ((float) (kAmountSteps - 1), amount * 2.0f));
    const float ii = std::max (0.0f, std::min ((float) (kInputSteps - 1), (inputDb + 18.0f) / 6.0f));
    float hi = 0.0f;
    if (hpfOn)
    {
        const float lf = std::log (std::max (20.0f, std::min (15000.0f, hpfHz)));
        int j = 0;
        while (j < 7 && lf > std::log (kHpfHz[j + 1])) ++j;
        const float t = (lf - std::log (kHpfHz[j])) / (std::log (kHpfHz[j + 1]) - std::log (kHpfHz[j]));
        hi = 1.0f + (float) j + std::max (0.0f, std::min (1.0f, t));
    }
    auto at = [&] (int i, int h, int a) { return T[mode][i][h][a]; };
    auto lerpA = [&] (int i, int h)
    {
        const int a0 = (int) ai, a1 = std::min (kAmountSteps - 1, a0 + 1);
        const float fa = ai - (float) a0;
        return at (i, h, a0) + fa * (at (i, h, a1) - at (i, h, a0));
    };
    auto lerpH = [&] (int i)
    {
        if (! hpfOn) return lerpA (i, 0);
        const int h0 = std::max (1, std::min (kHpfSteps - 1, (int) hi));
        const int h1 = std::min (kHpfSteps - 1, h0 + 1);
        const float fh = std::max (0.0f, std::min (1.0f, hi - (float) h0));
        return lerpA (i, h0) + fh * (lerpA (i, h1) - lerpA (i, h0));
    };
    const int i0 = (int) ii, i1 = std::min (kInputSteps - 1, i0 + 1);
    const float fi = ii - (float) i0;
    return lerpH (i0) + fi * (lerpH (i1) - lerpH (i0));
}

float VintageEngine::vintageCompDb (int mode, float amount, float hpfHz, bool hpfOn, float lpfHz, bool lpfOn, float inputDb) noexcept
{
    const float full = tableCompDb (mode, amount, hpfHz, hpfOn, inputDb);
    if (! lpfOn) return full;
    const float edge = std::max (lpfHz, hpfOn ? hpfHz : 20.0f);
    if (edge >= 20000.0f) return full;
    constexpr float topKnot = agc::kLpfHz[agc::kLpfSteps - 1];
    if (edge > topKnot)
    {
        // above the last calibrated low-pass point, ease from its value to the open-band value at 20 kHz
        const float atKnot = bandCompDb (mode, amount, hpfHz, hpfOn, topKnot, inputDb, full);
        const float t = std::log (edge / topKnot) / std::log (20000.0f / topKnot);
        return atKnot + t * (full - atKnot);
    }
    return bandCompDb (mode, amount, hpfHz, hpfOn, edge, inputDb, full);
}

float VintageEngine::bandCompDb (int mode, float amount, float hpfHz, bool hpfOn, float edge, float inputDb, float full) noexcept
{
    // The tables were calibrated with the band open to the top. With the low-pass edge in, take away
    // what the part above it contributed, treating the loudness changes of the two parts as adding
    // in power: R(band) = 1 + R(from hpf up) - R(from lpf up), with R = 10^(-comp/10); then add the
    // calibrated residual for what that model misses (strong saturation of a narrow band).
    auto toR = [] (float compDb) { return std::pow (10.0f, -0.1f * compDb); };
    float rAbove;
    if (edge <= agc::kHpfHz[8])
        rAbove = toR (tableCompDb (mode, amount, edge, true, inputDb));
    else
    {
        const float t = std::log (edge / agc::kHpfHz[8]) / std::log (20000.0f / agc::kHpfHz[8]);
        const float r15 = toR (tableCompDb (mode, amount, agc::kHpfHz[8], true, inputDb));
        rAbove = r15 + t * (1.0f - r15);
    }
    const float rBand = std::max (0.05f, 1.0f + toR (full) - rAbove);
    const float model = -10.0f * std::log10 (rBand);
    return useLpfResidual ? model + lpfResidualDb (mode, amount, hpfHz, hpfOn, edge, inputDb) : model;
}

float VintageEngine::lpfResidualDb (int mode, float amount, float hpfHz, bool hpfOn, float lpfHz, float inputDb) noexcept
{
    // what the model above misses (strong saturation of a narrow band), measured offline per knot
    using namespace agc;
    const auto& T = lpfResidualTable();
    mode = std::max (0, std::min (2, mode));

    const float ii = std::max (0.0f, std::min ((float) (kInputSteps - 1), (inputDb + 18.0f) / 6.0f));
    const int i0 = (int) ii, i1 = std::min (kInputSteps - 1, i0 + 1);
    const float fi = ii - (float) i0;

    int h0 = 0, h1 = 0;
    float fh = 0.0f;
    if (hpfOn)
    {
        const float lf = std::log (std::max (kHpfHz[0], std::min (kHpfHz[8], hpfHz)));
        int j = 0;
        while (j < 7 && lf > std::log (kHpfHz[j + 1])) ++j;
        h0 = 1 + j;
        h1 = 2 + j;
        fh = std::max (0.0f, std::min (1.0f, (lf - std::log (kHpfHz[j])) / (std::log (kHpfHz[j + 1]) - std::log (kHpfHz[j]))));
    }

    // low-pass knots in log frequency; above the last one the correction fades out by 20 kHz
    int l0 = 0, l1 = 0;
    float fl = 0.0f, fade = 1.0f;
    const float lf = std::log (std::max (kLpfHz[0], lpfHz));
    if (lpfHz >= kLpfHz[kLpfSteps - 1])
    {
        l0 = l1 = kLpfSteps - 1;
        fade = 1.0f - std::max (0.0f, std::min (1.0f, std::log (lpfHz / kLpfHz[kLpfSteps - 1]) / std::log (20000.0f / kLpfHz[kLpfSteps - 1])));
    }
    else
    {
        int j = 0;
        while (j < kLpfSteps - 2 && lf > std::log (kLpfHz[j + 1])) ++j;
        l0 = j;
        l1 = j + 1;
        fl = std::max (0.0f, std::min (1.0f, (lf - std::log (kLpfHz[j])) / (std::log (kLpfHz[j + 1]) - std::log (kLpfHz[j]))));
    }

    int a0 = 0;
    while (a0 < kLpfAmountSteps - 2 && amount > kLpfAmounts[a0 + 1]) ++a0;
    const int a1 = a0 + 1;
    const float fa = std::max (0.0f, std::min (1.0f, (amount - kLpfAmounts[a0]) / (kLpfAmounts[a1] - kLpfAmounts[a0])));

    auto at = [&] (int i, int h, int l)
    {
        const auto& row = T[mode][i][h][l];
        return row[a0] + fa * (row[a1] - row[a0]);
    };
    auto atL = [&] (int i, int h) { return at (i, h, l0) + fl * (at (i, h, l1) - at (i, h, l0)); };
    auto atH = [&] (int i) { return atL (i, h0) + fh * (atL (i, h1) - atL (i, h0)); };
    return fade * (atH (i0) + fi * (atH (i1) - atH (i0)));
}

float VintageEngine::tapeCompDb (int model, float driveDb) noexcept
{
    using namespace agc;
    const auto& T = tapeTable();
    model = std::max (0, std::min (4, model));
    const float di = std::max (0.0f, std::min ((float) (kTapeDriveSteps - 1), (driveDb + 12.0f) / 2.0f));
    const int d0 = (int) di, d1 = std::min (kTapeDriveSteps - 1, d0 + 1);
    return T[model][d0] + (di - (float) d0) * (T[model][d1] - T[model][d0]);
}

void VintageEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    numCh = std::max (1, std::min (2, numChannels));

    suppressor.prepare (fs, numCh);
    split.prepare (fs, numCh);
    harmonics.prepare (fs, numCh, maxBlock);
    tape.prepare (fs, numCh, maxBlock);

    vintageLatency = suppressor.getLatencySamples() + split.getLatencySamples() + harmonics.getLatencySamples();
    totalLatency = vintageLatency + tape.getLatencySamples();

    dryIn.setSize (numCh, maxBlock, false, false, true);
    restBuf.setSize (numCh, maxBlock, false, false, true);
    bandBuf.setSize (numCh, maxBlock, false, false, true);
    for (int c = 0; c < 2; ++c)
    {
        restAlign[(size_t) c].prepare (harmonics.getLatencySamples() + 4);
        restAlign[(size_t) c].setDelay (harmonics.getLatencySamples());
        dryForMix[(size_t) c].prepare (vintageLatency + 4);
        dryForMix[(size_t) c].setDelay (vintageLatency);
        dryForBypass[(size_t) c].prepare (totalLatency + 4);
        dryForBypass[(size_t) c].setDelay (totalLatency);
    }
    inGain.reset (fs, 0.02, dbToGain (params.inputDb));
    outGain.reset (fs, 0.02, dbToGain (params.outputDb));
    mixRamp.reset (fs, 0.02, params.mix);
    compRamp.reset (fs, 0.05, 1.0f);
    deltaRamp.reset (fs, 0.02, params.delta ? 1.0f : 0.0f);
    bypassRamp.reset (fs, 0.02, params.bypass ? 1.0f : 0.0f);
    tapeMakeup.reset (fs, 0.05, 1.0f);

    int rateFactor = 1;
    while (fs / rateFactor > 64000.0) rateFactor <<= 1;
    outN = 4096 * rateFactor;
    int order = 0; while ((1 << order) < outN) ++order;
    outFft = std::make_unique<juce::dsp::FFT> (order);
    outRing.assign ((size_t) outN, 0.0f);
    outWin.resize ((size_t) outN);
    double ws = 0.0;
    for (int i = 0; i < outN; ++i) { outWin[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / outN)); ws += outWin[(size_t) i]; }
    const float wn = (float) (2.0 / ws);
    for (auto& w : outWin) w *= wn;
    outSpec.assign ((size_t) (2 * outN), 0.0f);
    outPow.assign ((size_t) (outN / 2 + 1), 0.0f);
    outDbBins.assign ((size_t) (outN / 2 + 1), -140.0f);
    inRing.assign ((size_t) outN, 0.0f);
    inPow.assign ((size_t) (outN / 2 + 1), 0.0f);
    inDbBins.assign ((size_t) (outN / 2 + 1), -140.0f);
    displayInterval = (int) std::lround (fs / 30.0);
    reset();
}

void VintageEngine::reset()
{
    suppressor.reset();
    split.reset();
    harmonics.reset();
    tape.reset();
    for (auto* arr : { &restAlign, &dryForMix, &dryForBypass })
        for (auto& d : *arr) d.reset();
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    std::fill (outPow.begin(), outPow.end(), 0.0f);
    std::fill (inRing.begin(), inRing.end(), 0.0f);
    std::fill (inPow.begin(), inPow.end(), 0.0f);
    outRingPos = 0;
    samplesSinceDisplay = 0;
}

void VintageEngine::process (float* const* ch, int nChIn, int n) noexcept
{
    const int nCh = std::min (nChIn, numCh);
    if (n <= 0) return;
    if (n > maxBlock)
    {
        for (int start = 0; start < n; start += maxBlock)
        {
            const int len = std::min (maxBlock, n - start);
            float* sub[2] = { ch[0] + start, nCh > 1 ? ch[1] + start : nullptr };
            process (sub, nCh, len);
        }
        return;
    }

    const auto& p = params;
    // ---- parameters into the modules
    suppressor.setParams ({ p.amount, p.q, p.hpfHz, p.hpfOn, p.lpfHz, p.lpfOn });
    split.setBand (p.hpfHz, p.hpfOn, p.lpfHz, p.lpfOn);
    harmonics.setOversampling (p.osIndex);
    harmonics.setParams (p.amount, (HarmonicStage::Mode) std::max (0, std::min (2, p.mode)));
    tape.setOversampling (p.osIndex);
    tape.setParams (p.tapeOn && ! p.delta, p.tapeModel, p.tapeDriveDb);
    tape.setTransportEnabled (p.tapeTransport);

    inGain.setTarget (dbToGain (p.inputDb));
    outGain.setTarget (dbToGain (p.outputDb));
    mixRamp.setTarget (std::max (0.0f, std::min (1.0f, p.mix)));
    deltaRamp.setTarget (p.delta ? 1.0f : 0.0f);
    bypassRamp.setTarget (p.bypass ? 1.0f : 0.0f);
    float comp = 1.0f;
    if (p.autoGain)
        comp = dbToGain (-p.inputDb + vintageCompDb (p.mode, p.amount, p.hpfHz, p.hpfOn, p.lpfHz, p.lpfOn, p.inputDb));
    compRamp.setTarget (comp);
    float tapeGain = 1.0f;
    if (p.tapeOn && ! p.delta)
        tapeGain = p.autoGain ? dbToGain (tapeCompDb (p.tapeModel, p.tapeDriveDb)) : dbToGain (p.tapeDriveDb);
    tapeMakeup.setTarget (tapeGain);

    // ---- 1. keep the untouched input, apply INPUT
    for (int c = 0; c < nCh; ++c)
    {
        dryIn.copyFrom (c, 0, ch[c], n);
    }
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.next();
        for (int c = 0; c < nCh; ++c) ch[c][i] *= g;
    }

    // ---- 2. resonance suppression (full band, masked to the FILTER band)
    suppressor.process (ch, nCh, n);

    // ---- 3. band split, 4. harmonics on the FILTER band
    float* rest[2] = { restBuf.getWritePointer (0), nCh > 1 ? restBuf.getWritePointer (1) : nullptr };
    float* band[2] = { bandBuf.getWritePointer (0), nCh > 1 ? bandBuf.getWritePointer (1) : nullptr };
    split.process (ch, rest, band, nCh, n);
    harmonics.process (band, nCh, n);

    // ---- 5. sum, auto gain, mix, delta
    for (int i = 0; i < n; ++i)
    {
        const float cg = compRamp.next();
        const float mx = mixRamp.next();
        const float dl = deltaRamp.next();
        for (int c = 0; c < nCh; ++c)
        {
            const float wet = (restAlign[(size_t) c].process (rest[c][i]) + band[c][i]) * cg;
            const float dry = dryForMix[(size_t) c].process (dryIn.getSample (c, i));
            const float mixed = dry + mx * (wet - dry);
            ch[c][i] = mixed - dl * dry; // delta: only what the vintage section changed
        }
    }

    // ---- 6. MORE COLOUR tape (constant latency whether on or off)
    tape.process (ch, nCh, n);

    // ---- 7. tape makeup, OUTPUT, IN/OUT
    for (int i = 0; i < n; ++i)
    {
        const float tg = tapeMakeup.next();
        const float og = outGain.next();
        const float bp = bypassRamp.next();
        float drySum = 0.0f;
        for (int c = 0; c < nCh; ++c)
        {
            const float processed = ch[c][i] * tg * og;
            const float dry = dryForBypass[(size_t) c].process (dryIn.getSample (c, i));
            drySum += dry;
            const float y = processed + bp * (dry - processed);
            ch[c][i] = y;
        }
        // analysers: mono sum of what leaves the plugin, and of the input aligned to it
        float s = ch[0][i];
        if (nCh > 1) s = 0.5f * (s + ch[1][i]);
        outRing[(size_t) outRingPos] = s;
        inRing[(size_t) outRingPos] = drySum / (float) nCh;
        if (++outRingPos == outN) outRingPos = 0;
    }

    samplesSinceDisplay += n;
    if (samplesSinceDisplay >= displayInterval)
    {
        samplesSinceDisplay = 0;
        updateDisplay();
    }
}

void VintageEngine::mapBins (const float* binDb, int numBins, double binHz, float* out, bool takeMax) noexcept
{
    const int P = DisplayFrame::kPoints;
    for (int pt = 0; pt < P; ++pt)
    {
        const double f = displayFreqForPoint (pt);
        const double fLo = f * std::pow (1000.0, -0.5 / (P - 1)), fHi = f * std::pow (1000.0, 0.5 / (P - 1));
        int k0 = (int) std::ceil (fLo / binHz), k1 = (int) std::floor (fHi / binHz);
        k0 = std::max (1, k0);
        k1 = std::min (numBins - 1, k1);
        if (k1 >= k0)
        {
            float v = takeMax ? -1.0e9f : 0.0f;
            for (int k = k0; k <= k1; ++k)
                v = takeMax ? std::max (v, binDb[k]) : v + binDb[k];
            out[pt] = takeMax ? v : v / (float) (k1 - k0 + 1);
        }
        else
        {
            const double kf = std::min ((double) numBins - 1.001, std::max (1.0, f / binHz));
            const int k = (int) kf;
            const float fr = (float) (kf - k);
            out[pt] = binDb[k] + fr * (binDb[k + 1] - binDb[k]);
        }
    }
}

void VintageEngine::updateDisplay() noexcept
{
    // output spectrum from the ring buffer
    const int first = outN - outRingPos;
    for (int j = 0; j < first; ++j) outSpec[(size_t) j] = outRing[(size_t) (outRingPos + j)] * outWin[(size_t) j];
    for (int j = first; j < outN; ++j) outSpec[(size_t) j] = outRing[(size_t) (j - first)] * outWin[(size_t) j];
    std::fill (outSpec.begin() + outN, outSpec.end(), 0.0f);
    outFft->performRealOnlyForwardTransform (outSpec.data(), true);
    const int K = outN / 2 + 1;
    for (int k = 0; k < K; ++k)
    {
        const float re = outSpec[(size_t) (2 * k)], im = outSpec[(size_t) (2 * k + 1)];
        outPow[(size_t) k] += 0.5f * ((re * re + im * im) - outPow[(size_t) k]);
        outDbBins[(size_t) k] = powToDb (outPow[(size_t) k], -140.0f);
    }
    // input spectrum, same resolution and window as the output so the two compare directly
    for (int j = 0; j < first; ++j) outSpec[(size_t) j] = inRing[(size_t) (outRingPos + j)] * outWin[(size_t) j];
    for (int j = first; j < outN; ++j) outSpec[(size_t) j] = inRing[(size_t) (j - first)] * outWin[(size_t) j];
    std::fill (outSpec.begin() + outN, outSpec.end(), 0.0f);
    outFft->performRealOnlyForwardTransform (outSpec.data(), true);
    for (int k = 0; k < K; ++k)
    {
        const float re = outSpec[(size_t) (2 * k)], im = outSpec[(size_t) (2 * k + 1)];
        inPow[(size_t) k] += 0.5f * ((re * re + im * im) - inPow[(size_t) k]);
        inDbBins[(size_t) k] = powToDb (inPow[(size_t) k], -140.0f);
    }
    auto& frame = display.beginWrite();
    mapBins (inDbBins.data(), K, fs / outN, frame.inDb, true);
    mapBins (outDbBins.data(), K, fs / outN, frame.outDb, true);
    mapBins (suppressor.getDisplayGrDb(), suppressor.getNumBins(), suppressor.getBinHz(), frame.grDb, true);
    display.endWrite();
}

} // namespace uvp
