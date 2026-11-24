// Unique Vintage Processor — faceplate v4 controls
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

//==============================================================================
/** A slider whose double-click opens type-in instead of resetting (Alt/Option-click still resets). */
class ValueSlider final : public juce::Slider
{
public:
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (onDoubleClick != nullptr) onDoubleClick();
        else juce::Slider::mouseDoubleClick (e);
    }
};

/** Small type-in box that opens over a control. Enter or clicking away applies it, Esc cancels. */
class ValueEntry final : public juce::TextEditor
{
public:
    ValueEntry();
    void open (juce::Rectangle<int> area, const juce::String& text, std::function<void (const juce::String&)> onCommit);
    void dismiss (bool apply);
private:
    std::function<void (const juce::String&)> commit;
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

    ValueSlider slider;
    /** Places the knob so its centre lands on (cx, cy) in the parent. */
    void placeAt (float cx, float cy);
    /** Centre of the knob in the parent's coordinates. */
    juce::Point<float> getKnobCentre() const { return { (float) getX() + (float) getWidth() * 0.5f, (float) getY() + ringOuter() }; }
    float getKnobRadius() const { return bodyR; }
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
/** EVEN / ODD / HECTIC / MC: soft white box in the faceplate's type; the active one is solid white. */
class ModeButton final : public juce::Button
{
public:
    explicit ModeButton (const juce::String& label);
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** Rounded pill (BYPASS, DELTA, OS, FILTER): white (or a chosen colour), charcoal when on. */
class PillButton final : public juce::Button
{
public:
    PillButton (const juce::String& label, float textSize = 12.0f, float spacing = 1.4f, Weight w = Weight::SemiBold);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void setLabel (const juce::String& s) { setButtonText (s); repaint(); }
    void setOffColour (juce::Colour c) { offBody = c; repaint(); }
private:
    float size, spacing;
    Weight weight;
    juce::Colour offBody = col::white;
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
    static constexpr float kX0 = 70.0f, kX1 = 980.0f;           // 20 Hz .. 20 kHz, shared with the FILTER slider
    static float freqToX (float hz) { return kX0 + (kX1 - kX0) * std::log (hz / 20.0f) / std::log (1000.0f); }
    static float xToFreq (float x) { return 20.0f * std::pow (1000.0f, (x - kX0) / (kX1 - kX0)); }

    void setFrame (const uvp::DisplayFrame& f);
    /** The FILTER band: high-pass edge, low-pass edge (lpOn false = open to the top), and the switch. */
    void setBand (float hpHz, float lpHz, bool lpOn, bool on)
    {
        if (hpHz != hpfHz || lpHz != lpfHz || lpOn != lpfOn || on != filterOn)
        {
            hpfHz = hpHz; lpfHz = lpHz; lpfOn = lpOn; filterOn = on;
            repaint();
        }
    }
    void paint (juce::Graphics&) override;

private:
    float in[uvp::DisplayFrame::kPoints] {}, out[uvp::DisplayFrame::kPoints] {}, gr[uvp::DisplayFrame::kPoints] {};
    bool hasData = false;
    float hpfHz = 4000.0f, lpfHz = 20000.0f;
    bool lpfOn = false, filterOn = true;
};

//==============================================================================
/** FILTER: one track on the analyser's frequency axis with two handles - the high-pass edge (left) and
    the low-pass edge (right; off at the very top). The white stretch between them is what gets processed. */
class FilterRange final : public juce::Component
{
public:
    FilterRange (juce::RangedAudioParameter& hpf, juce::RangedAudioParameter& lpf, float lpfTopHz);

    static constexpr float kTrackY = 32.0f;       // local y of the track
    static constexpr float kHpfMaxHz = 15000.0f;

    /** Double-click on a handle (0 = high-pass, 1 = low-pass); `at` is the handle's centre, local. */
    std::function<void (int handle, juce::Point<float> at)> onDoubleClick;

    float handleX (int handle) const noexcept;
    float getHz (int handle) const noexcept { return handle == 0 ? hpHz : lpHz; }
    juce::RangedAudioParameter& paramFor (int handle) noexcept { return handle == 0 ? hp : lp; }
    /** Keeps the high-pass edge at or below the low-pass edge, each inside its own range. */
    float clampFor (int handle, float hz) const noexcept;
    bool isLowPassOff() const noexcept { return lpHz >= lpTop - 10.0f; }

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int nearestHandle (float x) const noexcept;
    juce::ParameterAttachment& attachmentFor (int handle) noexcept { return handle == 0 ? *hpAtt : *lpAtt; }

    juce::RangedAudioParameter& hp;
    juce::RangedAudioParameter& lp;
    const float lpTop;
    float hpHz = 4000.0f, lpHz = 20000.0f;
    std::unique_ptr<juce::ParameterAttachment> hpAtt, lpAtt;
    int dragging = -1;                 // -1 none, 0/1 a handle, -2 stacked handles: the first move decides
    float dragX = 0.0f, lastMouseX = 0.0f, downX = 0.0f;
};

} // namespace uvp::ui
