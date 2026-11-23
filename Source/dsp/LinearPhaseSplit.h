// Unique Vintage Processor — linear-phase band split
// Low = linear-phase FIR with a Linkwitz-Riley 24 dB/oct magnitude, High = delayed input - Low.
// The two bands always sum back to the (delayed) input exactly, at any cutoff.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DspCommon.h"

namespace uvp
{

class LinearPhaseSplit
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    int getLatencySamples() const noexcept { return B + L / 2; }

    /** Cutoff of the split and whether the split is active (inactive: everything goes high). */
    void setCutoff (float hz, bool enabled) noexcept { targetHz = hz; targetEnabled = enabled; }

    /** low/high may not alias `in`. */
    void process (const float* const* in, float* const* low, float* const* high, int numChannels, int numSamples) noexcept;

private:
    void designInto (int bank, double fc) noexcept;
    void processBlock() noexcept;

    double fs = 48000.0;
    int L = 4096, B = 256, F = 512, P = 16, numCh = 2;
    std::unique_ptr<juce::dsp::FFT> fftBlock, fftDesign;

    std::vector<float> designBuf, designTmp, window, scratch;
    std::vector<float> filt[2];                       // P partitions x (F) floats (interleaved complex, B+1 bins)
    std::vector<std::vector<float>> fdl;              // per channel: P x F
    std::vector<std::vector<float>> inBuf, prevBlock, lpOut;
    std::vector<IntDelay> dryDelay;
    std::vector<float> accA, accB;
    int fdlPos = 0, pos = 0;
    int curBank = 0;
    bool crossfadePending = false;
    double designedHz = -1.0;
    float targetHz = 4000.0f;
    bool targetEnabled = true;
    LinearRamp enableRamp;
};

} // namespace uvp
