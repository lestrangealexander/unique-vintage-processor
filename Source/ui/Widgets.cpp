// Unique Vintage Processor — faceplate v4 controls
#include "Widgets.h"

namespace uvp::ui
{

//==============================================================================
UVPLookAndFeel::UVPLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, col::white);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::headerTextColourId, col::ink.withAlpha (0.6f));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::yellow);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);
    setColour (juce::AlertWindow::backgroundColourId, col::white);
    setColour (juce::AlertWindow::textColourId, col::ink);
    setColour (juce::AlertWindow::outlineColourId, col::salmon);
    setColour (juce::TextButton::buttonColourId, col::yellow);
    setColour (juce::TextButton::buttonOnColourId, col::ink);
    setColour (juce::TextButton::textColourOffId, col::ink);
    setColour (juce::TextButton::textColourOnId, col::white);
    setColour (juce::TextEditor::backgroundColourId, col::white);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::outlineColourId, col::salmonDk);
    setColour (juce::TextEditor::focusedOutlineColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::yellow);
    setColour (juce::CaretComponent::caretColourId, col::ink);
}

void UVPLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
{
    const auto& pr = s.getProperties();
    const float bodyR = (float) pr.getWithDefault ("bodyR", 20.0);
    const float ringW = (float) pr.getWithDefault ("ringW", 4.0);
    const bool bipolar = (bool) pr.getWithDefault ("bipolar", false);
    const bool centreValue = (bool) pr.getWithDefault ("centreValue", false);
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    const float a0 = -135.0f, a1 = 135.0f, av = a0 + (a1 - a0) * pos;
    const float rr = bodyR + ringW * 0.5f + std::max (4.0f, bodyR * 0.06f);

    g.setColour (col::salmonDk);
    strokeRound (g, arc (cx, cy, rr, a0, a1), ringW);
    g.setColour (col::white);
    if (bipolar)
    {
        const float s0 = std::min (0.0f, av), s1 = std::max (0.0f, av);
        if (s1 - s0 > 0.5f)
            strokeRound (g, arc (cx, cy, rr, s0, s1), ringW);
        else
        {
            const auto p = polar (cx, cy, rr, 0.0f);
            g.fillEllipse (p.x - ringW * 0.5f, p.y - ringW * 0.5f, ringW, ringW);
        }
    }
    else if (av - a0 > 0.5f)
        strokeRound (g, arc (cx, cy, rr, a0, av), ringW);

    g.fillEllipse (cx - bodyR, cy - bodyR, 2.0f * bodyR, 2.0f * bodyR);
    const auto p1 = polar (cx, cy, bodyR * 0.42f, av), p2 = polar (cx, cy, bodyR * 0.8f, av);
    g.setColour (col::ink);
    juce::Path ptr;
    ptr.startNewSubPath (p1);
    ptr.lineTo (p2);
    strokeRound (g, ptr, std::max (2.6f, bodyR * 0.06f));

    if (centreValue && (s.isMouseOverOrDragging() || s.isMouseButtonDown()))
        drawText (g, s.getTextFromValue (s.getValue()), cx, cy + bodyR * 0.11f, font (Weight::Bold, bodyR * 0.26f), col::ink.withAlpha (0.75f));
}

void UVPLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                                       juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto& pr = s.getProperties();
    const bool fill = (bool) pr.getWithDefault ("fill", true);
    const bool valueOnHover = (bool) pr.getWithDefault ("valueOnHover", false);
    const int decimals = (int) pr.getWithDefault ("decimals", -1);
    const float cy = (float) (y + h) - 11.0f;
    const float x0 = (float) x, x1 = (float) (x + w);

    juce::Path track;
    track.startNewSubPath (x0, cy);
    track.lineTo (x1, cy);
    g.setColour (col::salmonDk);
    strokeRound (g, track, 6.0f);
    if (fill && sliderPos > x0 + 0.5f)
    {
        juce::Path f;
        f.startNewSubPath (x0, cy);
        f.lineTo (sliderPos, cy);
        g.setColour (col::white);
        strokeRound (g, f, 6.0f);
    }
    g.setColour (col::white);
    g.fillEllipse (sliderPos - 11.0f, cy - 11.0f, 22.0f, 22.0f);
    g.setColour (col::ink);
    g.fillEllipse (sliderPos - 3.2f, cy - 3.2f, 6.4f, 6.4f);

    if (! valueOnHover || s.isMouseOverOrDragging() || s.isMouseButtonDown())
    {
        const double v = s.getValue();
        juce::String t;
        if (decimals >= 0)
            t = juce::String (v, v < 10.0 ? decimals : 0);
        else
            t = s.getTextFromValue (v).toUpperCase();
        drawText (g, t, sliderPos, cy - 17.0f, font (Weight::SemiBold, 11.0f, valueOnHover ? 0.0f : 1.2f), col::ink.withAlpha (0.7f));
    }
}

juce::Font UVPLookAndFeel::getPopupMenuFont() { return font (Weight::Medium, 14.0f, 0.4f); }

void UVPLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (col::white);
    g.setColour (col::salmon.withAlpha (0.6f));
    g.drawRect (0, 0, width, height, 1);
}

juce::Font UVPLookAndFeel::getAlertWindowTitleFont() { return font (Weight::Bold, 16.0f, 1.2f); }
juce::Font UVPLookAndFeel::getAlertWindowMessageFont() { return font (Weight::Medium, 14.0f); }
juce::Font UVPLookAndFeel::getAlertWindowFont() { return font (Weight::Medium, 14.0f); }
juce::Font UVPLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (Weight::SemiBold, 13.0f, 1.2f); }

void UVPLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto c = bg;
    if (down) c = c.darker (0.08f);
    else if (over) c = c.brighter (0.04f);
    g.setColour (c);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
}

void UVPLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    g.setColour (te.findColour (juce::TextEditor::backgroundColourId).withAlpha (1.0f));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, std::min (10.0f, (float) h * 0.5f));
}

void UVPLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    const auto c = te.findColour (te.hasKeyboardFocus (true) ? juce::TextEditor::focusedOutlineColourId
                                                              : juce::TextEditor::outlineColourId);
    if (c.isTransparent()) return;
    g.setColour (c);
    g.drawRoundedRectangle (0.75f, 0.75f, (float) w - 1.5f, (float) h - 1.5f, std::min (10.0f, (float) h * 0.5f) - 0.75f, 1.5f);
}

//==============================================================================
ValueEntry::ValueEntry()
{
    setJustification (juce::Justification::centred);
    setFont (font (Weight::SemiBold, 14.0f, 0.3f));
    setIndents (6, 0);
    setBorder ({});
    setSelectAllWhenFocused (true);
    setScrollbarsShown (false);
    setPopupMenuEnabled (false);
    setInputRestrictions (16);
    // not quite opaque, so the rounded corners show the faceplate behind them
    setColour (juce::TextEditor::backgroundColourId, col::white.withAlpha (0.995f));
    setColour (juce::TextEditor::outlineColourId, col::salmonDk);
    setColour (juce::TextEditor::focusedOutlineColourId, col::ink.withAlpha (0.45f));
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::yellow);
    setColour (juce::TextEditor::highlightedTextColourId, col::ink);
    onReturnKey = [this] { dismiss (true); };
    onEscapeKey = [this] { dismiss (false); };
    onFocusLost = [this] { dismiss (true); };
    setVisible (false);
}

void ValueEntry::open (juce::Rectangle<int> area, const juce::String& text, std::function<void (const juce::String&)> onCommit)
{
    dismiss (true); // finish anything still open first
    commit = std::move (onCommit);
    setBounds (area);
    setText (text, false);
    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
    selectAll();
}

void ValueEntry::dismiss (bool apply)
{
    if (! isVisible()) return;
    auto cb = std::move (commit);
    commit = nullptr;
    const auto t = getText();
    setVisible (false);
    if (apply && cb != nullptr) cb (t);
}

//==============================================================================
ParamKnob::ParamKnob (const juce::String& n, float bodyRadius, float ringWidth, bool bipolar, float labelBaselineFromCentre,
                      float lSize, float lSpacing, Weight lWeight, bool centre)
    : name (n), bodyR (bodyRadius), ringW (ringWidth), labelOffset (labelBaselineFromCentre),
      labelSize (lSize), labelSpacing (lSpacing), labelWeight (lWeight), valueInCentre (centre)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (bodyRadius > 60.0f ? 320 : 220);
    slider.getProperties().set ("bodyR", bodyRadius);
    slider.getProperties().set ("ringW", ringWidth);
    slider.getProperties().set ("bipolar", bipolar);
    slider.getProperties().set ("centreValue", centre);
    slider.setScrollWheelEnabled (true);
    addAndMakeVisible (slider);
    slider.addMouseListener (this, false);
}

ParamKnob::~ParamKnob() { slider.removeMouseListener (this); }

void ParamKnob::placeAt (float cx, float cy)
{
    const float ro = ringOuter();
    const float halfW = std::max (ro, 46.0f);
    const float hgt = showLabel ? ro + labelOffset + 8.0f : 2.0f * ro;
    setBounds (juce::roundToInt (cx - halfW), juce::roundToInt (cy - ro), juce::roundToInt (2.0f * halfW), juce::roundToInt (hgt));
}

void ParamKnob::resized()
{
    const float ro = ringOuter();
    const float cx = (float) getWidth() * 0.5f;
    slider.setBounds (juce::Rectangle<float> (cx - ro, 0.0f, 2.0f * ro, 2.0f * ro).getSmallestIntegerContainer());
}

void ParamKnob::paint (juce::Graphics& g)
{
    if (! showLabel) return;
    const bool touching = slider.isMouseOverOrDragging() || slider.isMouseButtonDown();
    const auto s = (touching && ! valueInCentre) ? slider.getTextFromValue (slider.getValue()).toUpperCase() : name;
    drawText (g, s, (float) getWidth() * 0.5f, ringOuter() + labelOffset, font (labelWeight, labelSize, labelSpacing), col::ink);
}

//==============================================================================
ModeButton::ModeButton (const juce::String& label) : juce::Button (label)
{
    setClickingTogglesState (false);
}

void ModeButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    const auto b = getLocalBounds().toFloat();
    const float body = on ? 1.0f : (down ? 0.85f : (over ? 0.68f : 0.45f));
    g.setColour (col::white.withAlpha (body));
    g.fillRoundedRectangle (b, 12.0f);
    constexpr float size = 15.0f;
    drawText (g, getButtonText(), b.getCentreX(), b.getCentreY() + size * 0.36f, font (Weight::Bold, size, 3.0f),
              col::ink.withAlpha (on ? 1.0f : 0.6f));
}

PillButton::PillButton (const juce::String& label, float textSize, float sp, Weight w)
    : juce::Button (label), size (textSize), spacing (sp), weight (w) {}

void PillButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat();
    auto body = on ? col::ink : offBody;
    if (! on && (over || down)) body = offBody.interpolatedWith (offBody == col::white ? col::yellow : col::yellowLn, down ? 0.7f : 0.45f);
    if (on && over) body = col::ink.brighter (0.12f);
    g.setColour (body);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    drawText (g, getButtonText(), b.getCentreX(), b.getCentreY() + size * 0.36f, font (weight, size, spacing), on ? col::white : col::ink);
}

RoundButton::RoundButton (const juce::String& label) : juce::Button (label) {}

void RoundButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat();
    auto body = on ? col::ink : col::white;
    if (! on && (over || down)) body = col::white.interpolatedWith (col::yellow, 0.45f);
    if (on && over) body = col::ink.brighter (0.12f);
    g.setColour (body);
    g.fillEllipse (b);
    drawText (g, getButtonText(), b.getCentreX(), b.getCentreY() + 13.0f * 0.36f, font (Weight::Bold, 13.0f, 0.6f), on ? col::white : col::ink);
}

ZoneButton::ZoneButton (const juce::String& label, bool leftHalf, bool withBurger)
    : juce::Button (label), left (leftHalf), burger (withBurger) {}

void ZoneButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat();
    if (over || down)
    {
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), 12.0f, 12.0f, left, ! left, left, ! left);
        g.setColour (col::yellow.withAlpha (down ? 0.9f : 0.6f));
        g.fillPath (p);
    }
    const auto f = font (Weight::SemiBold, 12.0f, 1.6f);
    if (burger)
    {
        g.setColour (col::ink);
        for (int i = 0; i < 3; ++i)
        {
            juce::Path l;
            l.startNewSubPath (15.0f, 7.0f + 5.0f * (float) i);
            l.lineTo (29.0f, 7.0f + 5.0f * (float) i);
            strokeRound (g, l, 2.0f);
        }
        drawText (g, getButtonText(), 40.0f, 16.6f, f, col::ink, Anchor::Start);
    }
    else
        drawText (g, getButtonText(), b.getCentreX(), 16.6f, f, col::ink);
}

ListItemButton::ListItemButton (const juce::String& label) : juce::Button (label) {}

void ListItemButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (on || over || down)
    {
        g.setColour (on ? col::yellow : col::yellow.withAlpha (0.45f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    }
    drawText (g, getButtonText(), 8.0f, 15.0f, font (on ? Weight::Bold : Weight::Medium, 12.0f, 1.3f),
          on ? col::ink : col::ink.withAlpha (0.8f), Anchor::Start);
}

ExpandButton::ExpandButton() : juce::Button ("EXPAND") {}

void ExpandButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool open = getToggleState();
    const auto c = col::ink.withAlpha (over ? 1.0f : 0.8f);
    drawText (g, open ? "CLOSE" : "EXPAND", 32.0f, 13.0f, font (Weight::SemiBold, 10.0f, 1.6f), c);
    juce::Path p;
    if (open) { p.startNewSubPath (24.0f, 29.0f); p.lineTo (32.0f, 21.0f); p.lineTo (40.0f, 29.0f); }
    else      { p.startNewSubPath (24.0f, 23.0f); p.lineTo (32.0f, 31.0f); p.lineTo (40.0f, 23.0f); }
    g.setColour (col::ink);
    g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void VuMeter::paint (juce::Graphics& g)
{
    const float r = (float) getWidth() * 0.5f;
    const float cx = r, cy = r;
    g.setColour (col::white);
    g.fillEllipse (0.0f, 0.0f, 2.0f * r, 2.0f * r);
    const float px = cx, py = cy + r * 0.6f;
    const float bandR = r * 1.04f - 6.0f;
    auto ang = [] (float gdb) { return -50.0f + 100.0f * (1.0f - std::pow (std::min (std::max (gdb, 0.0f), 20.0f) / 20.0f, 0.75f)); };

    g.setColour (col::yellow);
    strokeRound (g, arc (px, py, bandR, ang (20.0f), ang (0.0f)), 13.0f);
    if (gr > 0.05f)
    {
        g.setColour (col::salmon);
        strokeRound (g, arc (px, py, bandR, ang (gr), ang (0.0f)), 13.0f);
    }
    for (int gdb : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 20 })
    {
        const float a = ang ((float) gdb);
        const bool major = gdb == 0 || gdb == 2 || gdb == 5 || gdb == 10 || gdb == 20;
        juce::Path t;
        t.startNewSubPath (polar (px, py, bandR - 13.0f, a));
        t.lineTo (polar (px, py, bandR - (major ? 22.0f : 18.0f), a));
        g.setColour (col::ink.withAlpha (major ? 0.4f : 0.22f));
        strokeRound (g, t, major ? 1.8f : 1.2f);
        if (major)
        {
            const auto pt = polar (px, py, bandR - 36.0f, a);
            drawText (g, juce::String (gdb), pt.x, pt.y + 4.5f, font (Weight::SemiBold, 13.0f), col::ink.withAlpha (0.6f));
        }
    }
    drawText (g, "dB", cx, py + 27.0f, font (Weight::SemiBold, 11.0f, 1.2f), col::ink.withAlpha (0.4f));
    const float an = ang (gr);
    juce::Path needle;
    needle.startNewSubPath (polar (px, py, 13.0f, an));
    needle.lineTo (polar (px, py, bandR + 4.0f, an));
    g.setColour (col::ink.withAlpha (0.85f));
    strokeRound (g, needle, 2.4f);
    g.setColour (col::salmon);
    g.fillEllipse (px - 9.0f, py - 9.0f, 18.0f, 18.0f);
    g.setColour (col::white);
    g.fillEllipse (px - 3.4f, py - 3.4f, 6.8f, 6.8f);
}

//==============================================================================
void Analyser::setFrame (const uvp::DisplayFrame& f)
{
    constexpr int K = uvp::DisplayFrame::kPoints;
    for (int p = 0; p < K; ++p)
    {
        // +3 dB/oct display tilt around 1 kHz (as the suppressor's detector hears it)
        const float tilt = 3.0f * std::log2 (uvp::VintageEngine::displayFreqForPoint (p) / 1000.0f);
        const float ni = f.inDb[p] + tilt, no = f.outDb[p] + tilt, ng = f.grDb[p];
        if (! hasData) { in[p] = ni; out[p] = no; gr[p] = ng; continue; }
        in[p] = ni > in[p] ? ni : std::max (ni, in[p] - 1.6f);
        out[p] = no > out[p] ? no : std::max (no, out[p] - 1.6f);
        gr[p] = ng > gr[p] ? ng : std::max (ng, gr[p] - 0.8f);
    }
    hasData = true;
    repaint();
}

void Analyser::paint (juce::Graphics& g)
{
    constexpr int K = uvp::DisplayFrame::kPoints;
    const float cardY0 = 4.0f, cardY1 = 286.0f;
    const juce::Rectangle<float> card (12.0f, cardY0, 976.0f, cardY1 - cardY0);
    g.setColour (col::white);
    g.fillRoundedRectangle (card, 16.0f);
    juce::Path clip;
    clip.addRoundedRectangle (card, 16.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    const float zeroY = cardY0 + 46.0f;
    auto grY = [zeroY] (float d) { return zeroY + d * 6.0f; };
    const float specTop = cardY0 + 90.0f, specBot = cardY1 - 24.0f;
    auto dbY = [specTop, specBot] (float d) { return specTop + juce::jlimit (0.0f, 1.0f, -d / 90.0f) * (specBot - specTop); };
    auto pointX = [] (int p) { return kX0 + (kX1 - kX0) * (float) p / (float) (K - 1); };

    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (col::ink.withAlpha (0.07f));
        g.drawVerticalLine (juce::roundToInt (freqToX (f)), cardY0 + 30.0f, cardY1);
    }
    for (int d : { 0, 6, 12, 18 })
    {
        g.setColour (col::ink.withAlpha (d == 0 ? 0.16f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (grY ((float) d)), kX0, kX1);
        drawText (g, d == 0 ? "0" : "-" + juce::String (d), 28.0f, grY ((float) d) + 4.5f, font (Weight::Medium, 11.0f), col::ink.withAlpha (0.5f), Anchor::Start);
    }
    const std::pair<float, const char*> labels[] = { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                                                     { 1000.0f, "1K" }, { 2000.0f, "2K" }, { 5000.0f, "5K" }, { 10000.0f, "10K" } };
    for (auto& [f, s] : labels)
        drawText (g, s, freqToX (f), cardY0 + 22.0f, font (Weight::Medium, 11.5f, 0.6f), col::ink.withAlpha (0.55f));

    // the FILTER band: shade what is left untouched on either side of it
    const bool lpIn = filterOn && lpfOn;
    const float hp = freqToX (filterOn ? juce::jlimit (20.0f, 20000.0f, hpfHz) : 20.0f);
    const float lp = std::max (hp, freqToX (lpIn ? juce::jlimit (20.0f, 20000.0f, lpfHz) : 20000.0f));
    g.setColour (col::salmon.withAlpha (0.12f));
    if (filterOn && hp > kX0)
        g.fillRect (juce::Rectangle<float> (kX0, cardY0 + 30.0f, hp - kX0, cardY1 - cardY0 - 30.0f));
    if (lpIn && lp < kX1)
        g.fillRect (juce::Rectangle<float> (lp, cardY0 + 30.0f, kX1 - lp, cardY1 - cardY0 - 30.0f));

    if (hasData)
    {
        juce::Path inFill, inLine, outLo, outHi, grFill, grLine;
        juce::Point<float> prevOut;
        int prevRegion = -1;
        for (int p = 0; p < K; ++p)
        {
            const float x = pointX (p), yi = dbY (in[p]), yo = dbY (out[p]);
            if (p == 0) { inFill.startNewSubPath (x, cardY1); inFill.lineTo (x, yi); inLine.startNewSubPath (x, yi); }
            else { inFill.lineTo (x, yi); inLine.lineTo (x, yi); }
            // output: faint outside the FILTER band (untouched there unless tape is on), solid inside it
            const int region = (x >= hp && x <= lp) ? 1 : 0;
            auto& op = region == 1 ? outHi : outLo;
            if (region != prevRegion)
                op.startNewSubPath (p > 0 ? prevOut : juce::Point<float> (x, yo));
            op.lineTo (x, yo);
            prevOut = { x, yo };
            prevRegion = region;
        }
        inFill.lineTo (kX1, cardY1);
        inFill.closeSubPath();
        g.setColour (col::yellow.withAlpha (0.85f));
        g.fillPath (inFill);
        g.setColour (col::yellowLn);
        g.strokePath (inLine, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ink.withAlpha (0.3f));
        g.strokePath (outLo, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ink.withAlpha (0.8f));
        g.strokePath (outHi, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        grFill.startNewSubPath (kX0, zeroY);
        for (int p = 0; p < K; ++p)
        {
            const float x = pointX (p), y = grY (std::min (gr[p], 30.0f));
            grFill.lineTo (x, y);
            if (p == 0) grLine.startNewSubPath (x, y); else grLine.lineTo (x, y);
        }
        grFill.lineTo (kX1, zeroY);
        grFill.closeSubPath();
        g.setColour (col::salmon.withAlpha (0.6f));
        g.fillPath (grFill);
        g.setColour (col::salmonDk);
        g.strokePath (grLine, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto dashAt = [&] (float x)
    {
        juce::Path dash, dashed;
        dash.startNewSubPath (x, cardY0);
        dash.lineTo (x, cardY1);
        const float pattern[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, dash, pattern, 2);
        g.setColour (col::ink.withAlpha (0.4f));
        g.fillPath (dashed);
    };
    if (filterOn && hp > kX0 + 0.5f) dashAt (hp);
    if (lpIn) dashAt (lp);

    const float lx = kX0 + 2.0f, ly = cardY1 - 9.0f;
    g.setColour (col::yellow);
    g.fillRoundedRectangle (lx, ly - 7.0f, 12.0f, 7.0f, 2.0f);
    g.setColour (col::yellowLn);
    g.drawRoundedRectangle (lx, ly - 7.0f, 12.0f, 7.0f, 2.0f, 0.8f);
    const auto lf = font (Weight::SemiBold, 10.0f, 1.2f);
    drawText (g, "INPUT", lx + 18.0f, ly, lf, col::ink.withAlpha (0.55f), Anchor::Start);
    juce::Path sw;
    sw.startNewSubPath (lx + 70.0f, ly - 3.5f);
    sw.lineTo (lx + 84.0f, ly - 3.5f);
    g.setColour (col::ink.withAlpha (0.8f));
    strokeRound (g, sw, 1.6f);
    drawText (g, "OUTPUT", lx + 90.0f, ly, lf, col::ink.withAlpha (0.55f), Anchor::Start);
    g.setColour (col::salmon);
    g.fillRoundedRectangle (lx + 160.0f, ly - 7.0f, 12.0f, 7.0f, 2.0f);
    drawText (g, "SUPPRESSION", lx + 178.0f, ly, lf, col::ink.withAlpha (0.55f), Anchor::Start);
    g.restoreState();
}

//==============================================================================
FilterRange::FilterRange (juce::RangedAudioParameter& hpf, juce::RangedAudioParameter& lpf, float lpfTopHz)
    : hp (hpf), lp (lpf), lpTop (lpfTopHz)
{
    hpAtt = std::make_unique<juce::ParameterAttachment> (hp, [this] (float v) { hpHz = v; repaint(); });
    lpAtt = std::make_unique<juce::ParameterAttachment> (lp, [this] (float v) { lpHz = v; repaint(); });
    hpAtt->sendInitialUpdate();
    lpAtt->sendInitialUpdate();
}

float FilterRange::handleX (int handle) const noexcept
{
    return Analyser::freqToX (juce::jlimit (20.0f, lpTop, handle == 0 ? hpHz : lpHz));
}

float FilterRange::clampFor (int handle, float hz) const noexcept
{
    if (handle == 0)
        return juce::jlimit (20.0f, std::min (kHpfMaxHz, lpHz), hz);
    return juce::jlimit (std::min (lpTop, std::max (20.0f, hpHz)), lpTop, hz);
}

int FilterRange::nearestHandle (float x) const noexcept
{
    const float xh = handleX (0), xl = handleX (1);
    const float dh = std::abs (x - xh), dl = std::abs (x - xl);
    if (std::abs (dh - dl) < 0.5f)
        return x < xh ? 0 : 1;
    return dh < dl ? 0 : 1;
}

bool FilterRange::hitTest (int x, int y)
{
    return x >= 58 && y >= 10; // the FILTER switch sits just left of the track
}

void FilterRange::paint (juce::Graphics& g)
{
    const float y = kTrackY;
    juce::Path track;
    track.startNewSubPath (Analyser::freqToX (20.0f), y);
    track.lineTo (Analyser::freqToX (lpTop), y);
    g.setColour (col::salmonDk);
    strokeRound (g, track, 6.0f);

    const float xh = handleX (0), xl = handleX (1);
    if (xl > xh + 0.5f)
    {
        juce::Path band; // what gets processed
        band.startNewSubPath (xh, y);
        band.lineTo (xl, y);
        g.setColour (col::white);
        strokeRound (g, band, 6.0f);
    }
    for (float x : { xh, xl })
    {
        g.setColour (col::white);
        g.fillEllipse (x - 11.0f, y - 11.0f, 22.0f, 22.0f);
        g.setColour (col::ink);
        g.fillEllipse (x - 3.2f, y - 3.2f, 6.4f, 6.4f);
    }

    // values above the handles, nudged apart when they meet and kept on the faceplate
    const auto f = font (Weight::SemiBold, 11.0f, 1.2f);
    const juce::String t0 = hp.getText (hp.convertTo0to1 (hpHz), 0).toUpperCase();
    const juce::String t1 = isLowPassOff() ? juce::String ("OFF") : lp.getText (lp.convertTo0to1 (lpHz), 0).toUpperCase();
    const float w0 = textWidth (f, t0), w1 = textWidth (f, t1), gap = 12.0f;
    float c0 = xh, c1 = xl;
    const float overlap = (c0 + 0.5f * w0 + gap) - (c1 - 0.5f * w1);
    if (overlap > 0.0f) { c0 -= 0.5f * overlap; c1 += 0.5f * overlap; }
    c1 = std::min (c1, (float) getWidth() - 4.0f - 0.5f * w1);
    c0 = std::min (c0, c1 - 0.5f * (w0 + w1) - gap);
    c0 = std::max (c0, 4.0f + 0.5f * w0);
    drawText (g, t0, c0, y - 17.0f, f, col::ink.withAlpha (0.7f));
    drawText (g, t1, c1, y - 17.0f, f, col::ink.withAlpha (isLowPassOff() ? 0.45f : 0.7f));
}

void FilterRange::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    if (e.mods.isPopupMenu()) return;
    const int h = nearestHandle (e.position.x);
    if (e.mods.isAltDown()) // reset: high-pass to its default, low-pass back off at the top
    {
        auto& p = paramFor (h);
        attachmentFor (h).setValueAsCompleteGesture (clampFor (h, p.convertFrom0to1 (p.getDefaultValue())));
        return;
    }
    const bool stacked = std::abs (handleX (0) - handleX (1)) < 1.0f && std::abs (e.position.x - handleX (0)) < 14.0f;
    dragging = stacked ? -2 : h;
    downX = lastMouseX = e.position.x;
    dragX = stacked ? handleX (0) : handleX (h);
    if (dragging >= 0) attachmentFor (dragging).beginGesture();
}

void FilterRange::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == -2)
    {
        // two handles on the same spot: dragging left takes the high-pass, right the low-pass
        const float dx = e.position.x - downX;
        if (std::abs (dx) < 2.0f) return;
        dragging = dx < 0.0f ? 0 : 1;
        attachmentFor (dragging).beginGesture();
    }
    if (dragging < 0) return;
    dragX += (e.position.x - lastMouseX) * (e.mods.isShiftDown() ? 0.2f : 1.0f); // shift: fine
    lastMouseX = e.position.x;
    attachmentFor (dragging).setValueAsPartOfGesture (clampFor (dragging, Analyser::xToFreq (dragX)));
}

void FilterRange::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0) attachmentFor (dragging).endGesture();
    dragging = -1;
}

void FilterRange::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = nearestHandle (e.position.x);
    if (onDoubleClick != nullptr) onDoubleClick (h, { handleX (h), kTrackY });
}

void FilterRange::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float dy = w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (dy) < 1.0e-4f) return;
    const int h = nearestHandle (e.position.x);
    attachmentFor (h).setValueAsCompleteGesture (clampFor (h, getHz (h) * std::pow (2.0f, 1.5f * dy)));
}

} // namespace uvp::ui
