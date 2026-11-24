// Unique Vintage Processor — linear-phase band split (the FILTER section)
//
// Two linear-phase FIR low-passes with a Linkwitz-Riley 24 dB/oct magnitude, one at each edge of
// the processed band:  rest = LP(hpf) + (x - LP(lpf)),  band = delayed input - rest.
// The two parts always sum back to the (delayed) input exactly, at any setting.
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

    /** Edges of the processed band. A disabled edge lets that side of the spectrum into the band. */
    void setBand (float hpfHz, bool hpfEnabled, float lpfHz, bool lpfEnabled) noexcept
    {
        edges[0].targetHz = hpfHz; edges[0].targetEnabled = hpfEnabled;
        edges[1].targetHz = lpfHz; edges[1].targetEnabled = lpfEnabled;
    }

    /** rest/band may not alias `in`. rest + band == input delayed by getLatencySamples(). */
    void process (const float* const* in, float* const* rest, float* const* band, int numChannels, int numSamples) noexcept;

private:
    struct Edge
    {
        std::vector<float> filt[2];             // two banks of P partitions x 2F floats (complex spectra)
        std::vector<std::vector<float>> out;    // per channel: this block's low-pass output
        int curBank = 0;
        double designedHz = -1.0;
        float targetHz = 4000.0f;
        bool targetEnabled = true;
        bool valid = false;                     // `out` holds real filter output for the current block
        LinearRamp ramp;
    };

    void designInto (Edge& e, int bank, double fc) noexcept;
    void convolve (const std::vector<float>& H, int channel, std::vector<float>& acc) noexcept;
    void processBlock() noexcept;

    double fs = 48000.0;
    int L = 4096, B = 256, F = 512, P = 16, numCh = 2;
    std::unique_ptr<juce::dsp::FFT> fftBlock, fftDesign;

    std::vector<float> designBuf, designTmp, window, scratch;
    std::vector<std::vector<float>> fdl;              // per channel: P x 2F input spectra (shared by both edges)
    std::vector<std::vector<float>> inBuf, prevBlock;
    std::vector<IntDelay> dryDelay;
    std::vector<float> accA, accB;
    int fdlPos = 0, pos = 0;
    Edge edges[2];                                    // 0 = high-pass edge, 1 = low-pass edge
};

} // namespace uvp
