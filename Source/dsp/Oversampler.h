// Unique Vintage Processor — oversampler for the saturation stages
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace uvp
{
/** JUCE's "maximum quality" preset gives the first 2x stage a -75 dB stopband and each further
    stage 10 dB less (-45 dB for the last stage at 16x). That is fine for gentle saturation, but
    the slowly decaying harmonics of hysteresis and clipping then fold back into the audio band.
    Here every stage gets a -110 dB stopband. Stages above the first only have to keep the audio
    band clean (images and folds that would land below ~22 kHz), so their transition bands can
    be very wide and their filters stay short: it costs less CPU than the preset at 8x and 16x. */
inline std::unique_ptr<juce::dsp::Oversampling<float>> makeOversampler (size_t numChannels, int numStages, double baseRate)
{
    using OS = juce::dsp::Oversampling<float>;
    auto os = std::make_unique<OS> (numChannels);
    if (numStages <= 0)
        os->addDummyOversamplingStage();
    const double band = std::min (0.4535 * baseRate, 24000.0);
    for (int n = 0; n < numStages; ++n)
    {
        double tw = 0.06;
        if (n > 0)
        {
            const double rate = baseRate * std::pow (2.0, (double) (n + 1));   // rate the stage's filter runs at
            tw = std::clamp (2.0 * (0.25 - band / rate) - 0.01, 0.06, 0.42);
        }
        os->addOversamplingStage (OS::filterHalfBandFIREquiripple, (float) tw, -110.0f, (float) tw, -110.0f);
    }
    os->setUsingIntegerLatency (true);
    return os;
}
} // namespace uvp
