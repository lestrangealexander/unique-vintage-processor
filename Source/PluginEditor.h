// Unique Vintage Processor — editor (faceplate v4)
#pragma once

#include "PluginProcessor.h"
#include "ui/Widgets.h"

class UVPEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UVPEditor (UniqueVintageProcessor&);
    ~UVPEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int kWidth = 1000, kMainHeight = 660, kAnalyserHeight = 300;

    /** Opens or closes the analyser (as the EXPAND button does). */
    void showAnalyser (bool show) { setExpanded (show); }

private:
    class Faceplate;

    void timerCallback() override;
    void setExpanded (bool shouldExpand);
    void applySize();
    void showPresetMenu();
    void showOsMenu();
    void chooseAndSavePreset();
    void chooseAndLoadPreset();
    void showProblem (const juce::String& title, const juce::String& message);
    void refreshPresetLabel();

public:
    /** Saves the current settings as a preset file (what SAVE does once a file is chosen). */
    bool savePresetTo (const juce::File& file);

private:

    UniqueVintageProcessor& proc;
    uvp::ui::UVPLookAndFeel lnf;
    std::unique_ptr<Faceplate> face;
    bool expanded = false;
    int savedFlashTicks = 0;
    std::unique_ptr<juce::FileChooser> fileChooser;
    float vuValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UVPEditor)
};
