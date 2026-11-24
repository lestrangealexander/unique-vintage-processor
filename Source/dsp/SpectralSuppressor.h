// Unique Vintage Processor — dynamic resonance suppressor
// STFT "spectral dynamics" in the spirit of a lacquer-cutting HF/acceleration
// limiter combined with a resonance tamer: spiky, loud parts of the top end are
// pulled down per frequency, linked across channels, with fixed fast timing.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DspCommon.h"

namespace uvp
{

class SpectralSuppressor
{
public:
    struct Params
    {
        float amount = 0.0f;   // 0..11
        float q = 6.0f;        // 0.25 (broad) .. 40 (narrow)
        float hpfHz = 4000.0f;  // processing starts above this (when hpfOn)
        bool hpfOn = true;
        float lpfHz = 20000.0f; // ...and stops above this (when lpfOn)
        bool lpfOn = false;
    };

    void prepare (double sampleRate, int numChannels);
    void reset();
    int getLatencySamples() const noexcept { return N; }

    void setParams (const Params& p) noexcept { params = p; }

    /** In-place processing; channels share one detector (linked stereo). */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Peak suppression currently applied (dB, positive), for the needle meter. */
    float getMeterDb() const noexcept { return meterDb.load (std::memory_order_relaxed); }

    // Display access (audio thread only): latest smoothed input power (dB) and GR (dB) per bin.
    int getNumBins() const noexcept { return K; }
    double getBinHz() const noexcept { return binHz; }
    const float* getDisplayInputDb() const noexcept { return dispInDb.data(); }
    const float* getDisplayGrDb() const noexcept { return grSmooth.data(); }

    /** Debug/measurement: detector values at a bin after the last frame. */
    void debugBin (int k, float& spike, float& loud, float& gr) const noexcept
    {
        k = std::max (0, std::min (K - 1, k));
        spike = powToDb (bandLvl[(size_t) k] + 1.0e-20f) - 4.3429448f * refLvl[(size_t) k] - 2.0f;
        loud = powToDb (loudLvl[(size_t) k] * energyScale[(size_t) k] + 1.0e-20f);
        gr = grSmooth[(size_t) k];
    }

private:
    void processFrame() noexcept;
    void computeGains (float amount) noexcept;
    void updateKernelTables (float q) noexcept;
    void updateMask (const Params& p) noexcept;
    void boxSmooth3 (const float* in, float* out, const int* halfWidths) noexcept;

    Params params;
    double fs = 48000.0, binHz = 46.875;
    int N = 1024, H = 128, K = 513, order = 10, numCh = 2;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> window;
    std::vector<std::vector<float>> inFifo, outAccum, spec; // spec: 2N interleaved per channel
    int fifoPos = 0, hopCount = 0;

    std::vector<float> power, tilt, riaaW, mask, scratchA, scratchB, bandLvl, refLvl, loudLvl, energyScale;
    std::vector<float> grTarget, grSmooth, gains, dispPow, dispInDb;
    std::vector<int> hBand, hRef, hDet;
    std::vector<float> scratchSq, spread;
    std::vector<double> prefix;

    float lastQ = -1.0f, lastHpf = -1.0f, lastLpf = -1.0f;
    bool lastHpfOn = false, lastLpfOn = false;
    int kMin = 1;
    float attackCoef = 0.9f, releaseCoef = 0.1f, dispCoef = 0.3f, detCoef = 0.4f;
    float outScale = 1.0f / 3.0f, detScale = 1.0f;

    LinearRamp wetRamp;          // fades the STFT path in/out so amount 0 is bit-exact
    std::atomic<float> meterDb { 0.0f };
};

} // namespace uvp
