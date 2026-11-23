// Unique Vintage Processor — faceplate v4 controls
#pragma once

#include "Theme.h"
#include "../dsp/VintageEngine.h"

namespace uvp::ui
{

//==============================================================================
/** Knobs (white disc, salmon track, white value arc, charcoal pointer), sliders, menus. */
class UVPLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    UVPLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 11; }

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

//==============================================================================
/** A rotary knob with its label underneath; the label shows the value while you touch it. */
class ParamKnob final : public juce::Component
{
public:
    ParamKnob (const juce::String& name, float bodyRadius, float ringWidth, bool bipolar,
               float labelBaselineFromCentre, float labelSize, float labelSpacing,
               Weight labelWeight = Weight::SemiBold, bool valueInCentre = false);
    ~ParamKnob() override;

    juce::Slider slider;
    /** Places the knob so its centre lands on (cx, cy) in the parent. */
    void placeAt (float cx, float cy);
    void setShowLabel (bool show) { showLabel = show; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override { repaint(); }

    juce::String name;
    float bodyR, ringW, labelOffset, labelSize, labelSpacing;
    Weight labelWeight;
    bool valueInCentre;
    bool showLabel = true;
    float ringOuter() const { return bodyR + ringW * 0.5f + std::max (4.0f, bodyR * 0.06f) + ringW * 0.5f; }
};

//==============================================================================
/** EVEN / ODD / HECTIC / MC: framed box with heavy rounded italic capitals. */
class OasisButton final : public juce::Button
{
public:
    explicit OasisButton (const juce::String& label, float textSize = 23.0f);
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    float size;
};

/** Rounded pill (BYPASS, DELTA, OS, HPF): white, or charcoal when on. */
class PillButton final : public juce::Button
{
public:
    PillButton (const juce::String& label, float textSize = 12.0f, float spacing = 1.4f, Weight w = Weight::SemiBold);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void setLabel (const juce::String& s) { setButtonText (s); repaint(); }
private:
    float size, spacing;
    Weight weight;
};

/** AG: round button, charcoal when on. */
class RoundButton final : public juce::Button
{
public:
    explicit RoundButton (const juce::String& label);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** MENU / SAVE halves of the white pill in the top strip. */
class ZoneButton final : public juce::Button
{
public:
    ZoneButton (const juce::String& label, bool leftHalf, bool burger);
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    bool left, burger;
};

/** One tape machine in the MORE COLOUR list. */
class ListItemButton final : public juce::Button
{
public:
    explicit ListItemButton (const juce::String& label);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** EXPAND / CLOSE with a chevron. */
class ExpandButton final : public juce::Button
{
public:
    ExpandButton();
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

//==============================================================================
/** Soft pastel suppression meter (peak gain reduction, VU-style ballistics). */
class VuMeter final : public juce::Component
{
public:
    void setReduction (float db) { if (std::abs (db - gr) > 0.01f) { gr = db; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    float gr = 0.0f;
};

//==============================================================================
/** Spectrum view: input (yellow), output (charcoal line), suppression (salmon, from the top). */
class Analyser final : public juce::Component
{
public:
    static constexpr float kX0 = 70.0f, kX1 = 980.0f;           // shared with the HPF slider
    static float freqToX (float hz) { return kX0 + (kX1 - kX0) * std::log (hz / 20.0f) / std::log (1000.0f); }

    void setFrame (const uvp::DisplayFrame& f);
    void setHpf (float hz, bool on) { if (hz != hpfHz || on != hpfOn) { hpfHz = hz; hpfOn = on; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    float in[uvp::DisplayFrame::kPoints] {}, out[uvp::DisplayFrame::kPoints] {}, gr[uvp::DisplayFrame::kPoints] {};
    bool hasData = false;
    float hpfHz = 4000.0f;
    bool hpfOn = true;
};

} // namespace uvp::ui
