// Unique Vintage Processor — complete signal chain
//
//  in -> INPUT -> suppressor (STFT, linked, masked to the FILTER band) -> LR4 linear-phase band split
//        band (HPF..LPF) -> harmonic stage (oversampled, RIAA emphasis)  ┐
//        rest (outside)  -> latency align ──────────────────────────────┴-> sum
//     -> AUTO GAIN -> DELTA -> MORE COLOUR tape -> OUTPUT -> MIX (vs. the untouched input) -> IN/OUT
//
// Latency is constant for every setting (all optional paths are padded).
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "SpectralSuppressor.h"
#include "LinearPhaseSplit.h"
#include "HarmonicStage.h"
#include "TapeMachine.h"

namespace uvp
{

struct EngineParams
{
    float inputDb = 0.0f, outputDb = 0.0f;
    bool autoGain = true;
    float amount = 0.0f;
    int mode = 0;            // 0 even, 1 odd, 2 hectic
    float q = 6.0f;
    float hpfHz = 4000.0f;
    bool hpfOn = true;       // FILTER: high-pass edge of the processed band
    float lpfHz = 20000.0f;
    bool lpfOn = false;      // FILTER: low-pass edge of the processed band
    int osIndex = 2;         // 0..4 -> 1x..16x
    float mix = 1.0f;        // 0..1
    bool delta = false;
    bool bypass = false;
    bool tapeOn = false;
    int tapeModel = 1;
    float tapeDriveDb = 0.0f;
    bool tapeTransport = true;   // test hook: false freezes wow/flutter/dropouts for steady-state measurements
};

struct DisplayFrame
{
    static constexpr int kPoints = 256;
    float inDb[kPoints];
    float outDb[kPoints];
    float grDb[kPoints];
};

class VintageEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    int getLatencySamples() const noexcept { return totalLatency; }

    void setParams (const EngineParams& p) noexcept { params = p; }
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    float getSuppressionMeterDb() const noexcept { return suppressor.getMeterDb(); }
    TripleBuffer<DisplayFrame>& getDisplay() noexcept { return display; }

    static float displayFreqForPoint (int p) noexcept
    {
        return 20.0f * std::pow (1000.0f, (float) p / (float) (DisplayFrame::kPoints - 1));
    }

    /** Static loudness compensation (dB) for the vintage section, from offline calibration. */
    static float vintageCompDb (int mode, float amount, float hpfHz, bool hpfOn, float lpfHz, bool lpfOn, float inputDb) noexcept;
    /** Test hook: leave out the calibrated low-pass correction (used while calibrating it). */
    static inline bool useLpfResidual = true;
    /** Static loudness compensation (dB) for the tape stage (model, drive). */
    static float tapeCompDb (int model, float driveDb) noexcept;

private:
    static float tableCompDb (int mode, float amount, float hpfHz, bool hpfOn, float inputDb) noexcept;
    static float bandCompDb (int mode, float amount, float hpfHz, bool hpfOn, float edgeHz, float inputDb, float fullDb) noexcept;
    static float lpfResidualDb (int mode, float amount, float hpfHz, bool hpfOn, float lpfHz, float inputDb) noexcept;
    void updateDisplay() noexcept;
    void mapBins (const float* binDb, int numBins, double binHz, float* outPoints, bool takeMax) noexcept;

    EngineParams params;
    double fs = 48000.0;
    int numCh = 2, maxBlock = 512;
    SpectralSuppressor suppressor;
    LinearPhaseSplit split;
    HarmonicStage harmonics;
    TapeMachine tape;
    int vintageLatency = 0, totalLatency = 0;

    juce::AudioBuffer<float> dryIn, restBuf, bandBuf;
    std::array<IntDelay, 2> restAlign, dryForDelta, dryForBypass;
    LinearRamp inGain, outGain, mixRamp, compRamp, deltaRamp, deltaMixRamp, bypassRamp, tapeMakeup;

    // output analyser
    std::unique_ptr<juce::dsp::FFT> outFft;
    int outN = 4096;
    std::vector<float> outRing, outWin, outSpec, outPow, outDbBins;
    std::vector<float> inRing, inPow, inDbBins;   // input analyser (latency-aligned dry signal)
    int outRingPos = 0, samplesSinceDisplay = 0, displayInterval = 1600;
    TripleBuffer<DisplayFrame> display;
};

} // namespace uvp
