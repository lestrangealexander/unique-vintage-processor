// Unique Vintage Processor — Sound Aquarium
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamText.h"

namespace
{
/** Q fader: 0.25 (broad) .. 40 (narrow), skewed so the centre of the fader sits at ~Q 6. */
juce::NormalisableRange<float> qRange()
{
    constexpr float lo = 0.25f, hi = 40.0f, g = 0.675f;
    return { lo, hi,
             [] (float, float, float t) { return lo * std::pow (hi / lo, std::pow (juce::jlimit (0.0f, 1.0f, t), g)); },
             [] (float, float, float v) { return std::pow (std::log (juce::jlimit (lo, hi, v) / lo) / std::log (hi / lo), 1.0f / g); },
             [] (float, float, float v) { return juce::jlimit (lo, hi, v); } };
}

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float t) { return s * std::pow (e / s, t); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

juce::String dbText (float v, int)
{
    return (v > 0.04f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
}

juce::String hzText (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout UniqueVintageProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto fl = [] (const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> txt, const char* label = "")
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (txt).withLabel (label));
    };
    layout.add (fl (ParamIDs::input, "Input", { -18.0f, 18.0f, 0.1f }, 0.0f, dbText));
    layout.add (fl (ParamIDs::output, "Output", { -18.0f, 18.0f, 0.1f }, 0.0f, dbText));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", true));
    layout.add (fl (ParamIDs::amount, "Amount", { 0.0f, 11.0f, 0.01f }, 0.0f,
                    [] (float v, int) { return String (v, 1); }));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mode, 1 }, "Harmonics", StringArray { "Even", "Odd", "Hectic" }, 0));
    layout.add (fl (ParamIDs::q, "Q", qRange(), 6.0f, [] (float v, int) { return String (v, v < 10.0f ? 2 : 1); }));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::hpfFreq, 1 }, "HPF", logRange (20.0f, 15000.0f), 4000.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (hzText)
                                                           .withValueFromStringFunction ([] (const String& t)
                                                           { return uvp::parseTypedValue (t, true).value_or (4000.0f); })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::hpfOn, 1 }, "Filter On", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::lpfFreq, 2 }, "LPF", logRange (20.0f, kLpfTopHz), kLpfTopHz,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int n)
                                                           { return lpfEngaged (v) ? hzText (v, n) : String ("Off"); })
                                                           .withValueFromStringFunction ([] (const String& t)
                                                           { return uvp::parseTypedValue (t, true, kLpfTopHz).value_or (kLpfTopHz); })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::oversampling, 1 }, "Oversampling",
                                                        StringArray { "1x", "2x", "4x", "8x", "16x" }, 2));
    layout.add (fl (ParamIDs::mix, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " %"; }));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::delta, 1 }, "Delta", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::moreColour, 1 }, "More Colour", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::tapeModel, 1 }, "Tape",
                                                        StringArray { "Tape Silk", "Tape Solid", "Tape Home", "Tape Garage", "Tape Cassette" }, 1));
    layout.add (fl (ParamIDs::tapeDrive, "Tape Drive", { -12.0f, 12.0f, 0.1f }, 0.0f, dbText));
    return layout;
}

UniqueVintageProcessor::UniqueVintageProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UVPState", createLayout()),
      presets (apvts)
{
    pInput = apvts.getRawParameterValue (ParamIDs::input);
    pOutput = apvts.getRawParameterValue (ParamIDs::output);
    pAutoGain = apvts.getRawParameterValue (ParamIDs::autoGain);
    pAmount = apvts.getRawParameterValue (ParamIDs::amount);
    pMode = apvts.getRawParameterValue (ParamIDs::mode);
    pQ = apvts.getRawParameterValue (ParamIDs::q);
    pHpfFreq = apvts.getRawParameterValue (ParamIDs::hpfFreq);
    pHpfOn = apvts.getRawParameterValue (ParamIDs::hpfOn);
    pLpfFreq = apvts.getRawParameterValue (ParamIDs::lpfFreq);
    pOs = apvts.getRawParameterValue (ParamIDs::oversampling);
    pMix = apvts.getRawParameterValue (ParamIDs::mix);
    pDelta = apvts.getRawParameterValue (ParamIDs::delta);
    pBypass = apvts.getRawParameterValue (ParamIDs::bypass);
    pMoreColour = apvts.getRawParameterValue (ParamIDs::moreColour);
    pTapeModel = apvts.getRawParameterValue (ParamIDs::tapeModel);
    pTapeDrive = apvts.getRawParameterValue (ParamIDs::tapeDrive);
    bypassParam = apvts.getParameter (ParamIDs::bypass);
}

bool UniqueVintageProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void UniqueVintageProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = std::max (1, std::min (2, getTotalNumOutputChannels()));
    engine.prepare (sampleRate, std::max (32, samplesPerBlock), ch);
    setLatencySamples (engine.getLatencySamples());
}

void UniqueVintageProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());

    uvp::EngineParams p;
    p.inputDb = pInput->load();
    p.outputDb = pOutput->load();
    p.autoGain = pAutoGain->load() > 0.5f;
    p.amount = pAmount->load();
    p.mode = (int) std::lround (pMode->load());
    p.q = pQ->load();
    p.hpfHz = pHpfFreq->load();
    p.hpfOn = pHpfOn->load() > 0.5f;                   // FILTER switches both edges
    p.lpfHz = pLpfFreq->load();
    p.lpfOn = p.hpfOn && lpfEngaged (p.lpfHz);
    p.osIndex = (int) std::lround (pOs->load());
    p.mix = pMix->load() * 0.01f;
    p.delta = pDelta->load() > 0.5f;
    p.bypass = pBypass->load() > 0.5f;
    p.tapeOn = pMoreColour->load() > 0.5f;
    p.tapeModel = (int) std::lround (pTapeModel->load());
    p.tapeDriveDb = pTapeDrive->load();
    engine.setParams (p);

    const int nCh = std::min (2, buffer.getNumChannels());
    engine.process (buffer.getArrayOfWritePointers(), nCh, buffer.getNumSamples());
}

void UniqueVintageProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.setProperty ("presetName", presets.currentName, nullptr);
    st.setProperty ("presetFile", presets.currentFile.getFullPathName(), nullptr);
    st.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void UniqueVintageProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            presets.currentName = tree.getProperty ("presetName", "default").toString();
            const auto path = tree.getProperty ("presetFile", {}).toString();
            presets.currentFile = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
            uiScale.store ((float) (double) tree.getProperty ("uiScale", 1.0));
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* UniqueVintageProcessor::createEditor()
{
    return new UVPEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UniqueVintageProcessor();
}
