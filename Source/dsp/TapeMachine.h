// Unique Vintage Processor — MORE COLOUR tape machines
// Each machine: record EQ -> magnetic hysteresis (oversampled) -> inverse record EQ ->
// playback response (head bump, LF/HF roll-off, head wear) -> transport (wow, flutter,
// scrape flutter) -> lived-in artefacts (azimuth drift, dropouts) for the last three.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "AnalogModels.h"

namespace uvp
{

struct TapeModelDef
{
    const char* name;
    double ips;
    double recZeroHz, recPoleHz;     // record EQ into the tape (sets HF saturation / self-erasure)
    JilesAtherton::Params core;      // tape magnetics
    double hDrive;                   // field per unit input at 0 VU calibration
    double elec2;                    // electronics 2nd-order coefficient
    double bumpHz, bumpDb, bumpQ, dipHz, dipDb, dipQ;
    double hpHz, hpQ, lpHz, lpQ;
    double wearShelfHz, wearShelfDb; // lived-in head wear (0 dB = none)
    // transport (speed deviation, fraction: 0.0003 = 0.03 %)
    double capPeak, capHz, reelPeak, reelHz, randRms, driftRms, scrapeRms;
    // lived-in
    double azimuthUs, dropoutsPerSec;
    bool livedIn;
};

const TapeModelDef& tapeModel (int index) noexcept;
constexpr int kNumTapeModels = 5;

class TapeMachine
{
public:
    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset();
    int getLatencySamples() const noexcept { return maxOsLatency + centreDelay; }

    /** The tape follows the global oversampling up to 8x: its magnetic stage is mild enough that
        8x (384 kHz) is already alias-free, so 16x would only cost CPU. */
    static constexpr int kMaxOsIndex = 3;
    void setOversampling (int index) noexcept { requestedOs = std::max (0, std::min (kMaxOsIndex, index)); }
    void setParams (bool enabled, int model, float driveDb) noexcept
    {
        targetOn = enabled;
        requestedModel = std::max (0, std::min (kNumTapeModels - 1, model));
        targetDriveDb = driveDb;
    }
    void process (float* const* ch, int numChannels, int numSamples) noexcept;

    /** Test hook: freeze the transport (no wow, flutter, dropouts or azimuth wander) so the
        steady-state response can be measured. Always on in the plugin. */
    void setTransportEnabled (bool on) noexcept { transportOn = on; }

    // offline calibration helper
    static double measureThd3 (int model, double fs, double levelDbfs);
    static double measureThd3 (const TapeModelDef& def, double hDrive, double fs, double levelDbfs);

private:
    struct Channel
    {
        FirstOrder rec, recInv;
        JilesAtherton ja;
        Biquad bump, dip, hp, lp, wear;
        FirstOrder dropLp;
        ModDelay delay;
        IntDelay dry;
    };
    void configure (int model, int osIndex);
    void updateTransport (int numSamples) noexcept;

    double fs = 48000.0;
    int numCh = 2, maxBlock = 512;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 5> os;
    std::array<int, 5> osLatency {};
    int maxOsLatency = 0, osIdx = 2, requestedOs = 2;
    int model = 1, requestedModel = 1;
    int centreDelay = 0;
    double fsOs = 192000.0, jaNorm = 1.0;
    std::array<Channel, 2> chans;
    juce::AudioBuffer<float> dryBuf;
    std::vector<float> wowDelay; // per-sample transport delay offset (samples)

    bool transportOn = true;
    bool targetOn = false;
    float targetDriveDb = 0.0f, currentDriveDb = 0.0f;
    LinearRamp onRamp, switchGain;
    int switchState = 0;

    // transport state
    XorShift rng;
    double capPhase = 0, reelPhase = 0, capHzJit = 0, reelHzJit = 0;
    double randState1 = 0, randState2 = 0, driftState = 0, driftState2 = 0, randInt = 0, driftInt = 0, scrapeInt = 0;
    Biquad scrapeBp;
    double azPhase = 0;
    // dropouts
    double dropTimer = 0, dropLen = 0, dropPos = 0, dropDepthUm = 0, dropRatioR = 1.0;
    bool dropActive = false;
    int dropUpdateCounter = 0;
};

} // namespace uvp
