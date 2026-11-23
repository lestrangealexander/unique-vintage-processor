// Unique Vintage Processor — harmonic stage (EVEN / ODD / HECTIC)
// Runs on the band above the HPF split. The nonlinearity sits inside an RIAA-style
// pre-/de-emphasis pair (75 us zero, 3.18 us pole) like a cut-and-played record: the very
// top drives hardest, then is rolled back, so the harmonics land smooth.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "AnalogModels.h"

namespace uvp
{

class HarmonicStage
{
public:
    enum Mode { Even = 0, Odd = 1, Hectic = 2 };
    static constexpr int kNumOsChoices = 5; // 1x 2x 4x 8x 16x

    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset();

    /** Constant regardless of oversampling choice (shorter paths are padded). */
    int getLatencySamples() const noexcept { return maxLatency; }

    void setOversampling (int index) noexcept { requestedOs = std::max (0, std::min (kNumOsChoices - 1, index)); }
    void setParams (float amount, Mode mode) noexcept { targetAmount = amount; requestedMode = mode; }

    /** In-place on the high band. */
    void process (float* const* ch, int numChannels, int numSamples) noexcept;

    // calibration helpers (offline)
    static double driveDbFor (Mode m, float amount) noexcept;
    static inline bool makeupEnabled = true; // switched off while measuring the make-up table

private:
    struct ChannelModels
    {
        FirstOrder pre, de;
        TriodeStage triode, triodeH;
        JilesAtherton ja, jaH;
        FirstOrder dc;
    };

    void configureForOs (int idx);
    inline double runModel (ChannelModels& m, Mode mode, double e, double drive) noexcept;

    double fs = 48000.0;
    int numCh = 2, maxBlock = 512;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kNumOsChoices> os;
    std::array<int, kNumOsChoices> osLatency {};
    int maxLatency = 0, osIdx = 2, requestedOs = 2;
    double fsOs = 192000.0;
    double jaNormOdd = 1.0, jaNormHectic = 1.0;

    std::array<ChannelModels, 2> models;
    std::array<IntDelay, 2> dryAlign, wetPad;
    juce::AudioBuffer<float> dryCopy;

    float targetAmount = 0.0f, currentAmount = 0.0f;
    Mode currentMode = Even, requestedMode = Even, fadeFromMode = Even;
    int modeFadeTotal = 1, modeFadeLeft = 0;
    // oversampling switch: fade the harmonic residual out, switch, fade back in
    int switchState = 0; // 0 idle, 1 fading out
    LinearRamp switchGain, wetRamp;
};

} // namespace uvp
