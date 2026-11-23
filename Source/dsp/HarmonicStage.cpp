// Unique Vintage Processor — harmonic stage
#include "HarmonicStage.h"
#include "HarmonicMakeup.h"

namespace uvp
{
namespace
{
    constexpr double kRiaaZeroHz = 2122.07;  // 75 us
    constexpr double kRiaaPoleHz = 50048.0;  // 3.18 us (Neumann limit)

    JilesAtherton::Params transformerCore() { return { 1.0, 1.0, 0.0, 0.05, 0.85 }; }
    JilesAtherton::Params hecticCore() { return { 1.0, 1.0, 0.0, 0.20, 0.60 }; }

} // namespace

double HarmonicStage::driveDbFor (Mode m, float a) noexcept
{
    a = std::max (0.0f, std::min (11.0f, a));
    // 0..8: heavy but musical; 8..11: overt distortion territory
    const double lo = a <= 8.0f ? a : 8.0;
    const double hi = a > 8.0f ? (a - 8.0) : 0.0;
    switch (m)
    {
        case Even:   return -10.0 + 3.5 * lo + 7.0 * hi;
        case Odd:    return -6.0 + 3.6 * lo + 7.0 * hi;
        case Hectic: return -18.0 + 3.6 * lo + 7.5 * hi;
    }
    return 0.0;
}

void HarmonicStage::prepare (double sampleRate, int numChannels, int maxBlockSize)
{
    fs = sampleRate;
    numCh = std::max (1, std::min (2, numChannels));
    maxBlock = std::max (1, maxBlockSize);

    maxLatency = 0;
    for (int i = 0; i < kNumOsChoices; ++i)
    {
        os[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, (size_t) i, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[(size_t) i]->initProcessing ((size_t) maxBlock);
        osLatency[(size_t) i] = (int) std::lround (os[(size_t) i]->getLatencyInSamples());
        maxLatency = std::max (maxLatency, osLatency[(size_t) i]);
    }
    for (int c = 0; c < 2; ++c)
    {
        dryAlign[(size_t) c].prepare (maxLatency + 8);
        dryAlign[(size_t) c].setDelay (maxLatency);
        wetPad[(size_t) c].prepare (maxLatency + 8);
    }
    dryCopy.setSize (numCh, maxBlock, false, false, true);

    jaNormOdd = JilesAtherton::measureSmallSignalGain (transformerCore(), 0.15);
    jaNormHectic = JilesAtherton::measureSmallSignalGain (hecticCore(), 0.15);

    switchGain.reset (fs, 0.006, 1.0f);
    wetRamp.reset (fs, 0.02, 0.0f);
    osIdx = requestedOs;
    configureForOs (osIdx);
    reset();
}

void HarmonicStage::configureForOs (int idx)
{
    fsOs = fs * (double) (1 << idx);
    for (auto& m : models)
    {
        m.pre.setZeroPole (fsOs, kRiaaZeroHz, kRiaaPoleHz);
        m.de.setInverseOf (m.pre);
        m.triode.prepare (fsOs);
        m.triodeH.prepare (fsOs);
        m.ja.setParams (transformerCore());
        m.jaH.setParams (hecticCore());
        m.dc.setHighPass (fs, 5.0);
    }
    for (int c = 0; c < 2; ++c)
        wetPad[(size_t) c].setDelay (maxLatency - osLatency[(size_t) idx]);
    modeFadeTotal = std::max (1, (int) std::lround (0.012 * fsOs));
}

void HarmonicStage::reset()
{
    for (auto& o : os) if (o) o->reset();
    for (auto& m : models)
    {
        m.pre.reset(); m.de.reset(); m.dc.reset();
        m.triode.reset(); m.triodeH.reset(); m.ja.reset(); m.jaH.reset();
    }
    for (auto& d : dryAlign) d.reset();
    for (auto& d : wetPad) d.reset();
    currentAmount = targetAmount;
    currentMode = fadeFromMode = requestedMode;
    modeFadeLeft = 0;
    switchState = 0;
    switchGain.snap (1.0f);
    wetRamp.snap (targetAmount > 0.0f ? 1.0f : 0.0f);
}

inline double HarmonicStage::runModel (ChannelModels& m, Mode mode, double e, double drive) noexcept
{
    switch (mode)
    {
        case Even:
            return m.triode.process (e * drive) / drive;
        case Odd:
        {
            const double M = m.ja.process (e * drive);
            return (M / jaNormOdd) / drive;
        }
        case Hectic:
        {
            // valve -> transformer -> silicon clipper, each stage unity gain for small signals
            const double v1 = m.triodeH.process (e * drive);
            const double v2 = m.jaH.process (v1) / jaNormHectic;
            const double v3 = DiodeClipper::process (v2 * 2.0);
            return v3 / (2.0 * drive);
        }
    }
    return e;
}

void HarmonicStage::process (float* const* ch, int nCh, int n) noexcept
{
    nCh = std::min (nCh, numCh);
    if (n <= 0) return;

    // oversampling changes: fade the residual out, switch, fade in
    if (requestedOs != osIdx && switchState == 0)
    {
        switchState = 1;
        switchGain.setTarget (0.0f);
    }
    if (switchState == 1 && ! switchGain.isRamping() && switchGain.getCurrent() <= 0.0f)
    {
        osIdx = requestedOs;
        configureForOs (osIdx);
        os[(size_t) osIdx]->reset();
        for (auto& m : models)
        {
            m.pre.reset(); m.de.reset();
            m.triode.reset(); m.triodeH.reset(); m.ja.reset(); m.jaH.reset();
        }
        for (auto& d : wetPad) d.reset();
        switchState = 0;
        switchGain.setTarget (1.0f);
    }

    if (requestedMode != currentMode && modeFadeLeft == 0)
    {
        fadeFromMode = currentMode;
        currentMode = requestedMode;
        modeFadeLeft = modeFadeTotal;
        for (int c = 0; c < nCh; ++c)
        {
            auto& m = models[(size_t) c];
            if (currentMode == Even) m.triode.reset();
            if (currentMode == Odd) m.ja.reset();
            if (currentMode == Hectic) { m.triodeH.reset(); m.jaH.reset(); }
        }
    }

    for (int c = 0; c < nCh; ++c)
        dryCopy.copyFrom (c, 0, ch[c], n);

    const float a0 = currentAmount, a1 = targetAmount;
    const double d0 = dbToGainD (driveDbFor (currentMode, a0)), d1 = dbToGainD (driveDbFor (currentMode, a1));
    const double f0 = dbToGainD (driveDbFor (fadeFromMode, a0)), f1 = dbToGainD (driveDbFor (fadeFromMode, a1));

    juce::dsp::AudioBlock<float> block (ch, (size_t) nCh, (size_t) n);
    auto up = os[(size_t) osIdx]->processSamplesUp (block);
    const int nUp = (int) up.getNumSamples();
    const int fadeStart = modeFadeLeft;

    for (int c = 0; c < nCh; ++c)
    {
        auto& m = models[(size_t) c];
        float* s = up.getChannelPointer ((size_t) c);
        int fadeLeft = fadeStart;
        for (int j = 0; j < nUp; ++j)
        {
            const double t = (double) (j + 1) / (double) nUp;
            const double drive = d0 + (d1 - d0) * t;
            const double e = m.pre.process ((double) s[j]);
            double y = runModel (m, currentMode, e, drive);
            if (fadeLeft > 0)
            {
                const double g = 1.0 - (double) fadeLeft / (double) modeFadeTotal;
                const double yOld = runModel (m, fadeFromMode, e, f0 + (f1 - f0) * t);
                y = yOld + g * (y - yOld);
                --fadeLeft;
            }
            s[j] = (float) m.de.process (y);
        }
    }
    modeFadeLeft = std::max (0, fadeStart - nUp);

    os[(size_t) osIdx]->processSamplesDown (block);

    const float mk0 = makeupEnabled ? dbToGain (hmk::makeupDb (currentMode, a0)) : 1.0f;
    const float mk1 = makeupEnabled ? dbToGain (hmk::makeupDb (currentMode, a1)) : 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float ti = (float) (i + 1) / (float) n;
        const float a = a0 + (a1 - a0) * ti;
        // ODD's magnetic core has a little low-field hysteresis distortion at any drive: fade it in
        // over the first few steps so the amount control stays progressive.
        const float modeFade = currentMode == Odd ? smoothstep (0.0f, 3.0f, a) : 1.0f;
        const float wa = smoothstep (0.0f, 0.25f, a) * modeFade * switchGain.next();
        const float mk = 1.0f + (mk0 + (mk1 - mk0) * ti - 1.0f) * switchGain.getCurrent();
        for (int c = 0; c < nCh; ++c)
        {
            auto& m = models[(size_t) c];
            const float xd = dryAlign[(size_t) c].process (dryCopy.getSample (c, i));
            const float y = wetPad[(size_t) c].process (ch[c][i]);
            const float r = (float) m.dc.process ((double) (y - xd));
            ch[c][i] = (xd + wa * r) * mk;
        }
    }
    currentAmount = a1;
}

} // namespace uvp
