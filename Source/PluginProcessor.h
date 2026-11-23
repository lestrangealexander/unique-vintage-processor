// Unique Vintage Processor — Sound Aquarium
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/VintageEngine.h"
#include "PresetManager.h"

namespace ParamIDs
{
inline constexpr const char* input = "input";
inline constexpr const char* output = "output";
inline constexpr const char* autoGain = "autoGain";
inline constexpr const char* amount = "amount";
inline constexpr const char* mode = "mode";
inline constexpr const char* q = "q";
inline constexpr const char* hpfFreq = "hpfFreq";
inline constexpr const char* hpfOn = "hpfOn";
inline constexpr const char* oversampling = "oversampling";
inline constexpr const char* mix = "mix";
inline constexpr const char* delta = "delta";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* moreColour = "moreColour";
inline constexpr const char* tapeModel = "tapeModel";
inline constexpr const char* tapeDrive = "tapeDrive";
} // namespace ParamIDs

class UniqueVintageProcessor final : public juce::AudioProcessor
{
public:
    UniqueVintageProcessor();
    ~UniqueVintageProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Unique Vintage Processor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    uvp::VintageEngine& getEngine() noexcept { return engine; }
    std::atomic<float> uiScale { 1.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    uvp::VintageEngine engine;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    std::atomic<float>* pInput = nullptr;  std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pAutoGain = nullptr; std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pMode = nullptr;   std::atomic<float>* pQ = nullptr;
    std::atomic<float>* pHpfFreq = nullptr; std::atomic<float>* pHpfOn = nullptr;
    std::atomic<float>* pOs = nullptr;     std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pDelta = nullptr;  std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pMoreColour = nullptr; std::atomic<float>* pTapeModel = nullptr;
    std::atomic<float>* pTapeDrive = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UniqueVintageProcessor)
};
