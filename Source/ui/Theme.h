// Unique Vintage Processor — faceplate v4 theme: palette, type and drawing helpers
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace uvp::ui
{
namespace col
{
    inline const juce::Colour salmon   { 0xFFFEA993 };
    inline const juce::Colour salmonDk { 0xFFEE917B };
    inline const juce::Colour yellow   { 0xFFFDE6B0 };
    inline const juce::Colour yellowLn { 0xFFEFCB86 };
    inline const juce::Colour ink      { 0xFF3A302C };
    inline const juce::Colour white    { 0xFFFFFFFF };
} // namespace col

enum class Weight { Medium, SemiBold, Bold, BlackItalic };

/** DM Sans at a CSS-style size (px) with CSS-style letter-spacing (px). */
juce::Font font (Weight w, float sizePx, float letterSpacingPx = 0.0f);

enum class Anchor { Start, Middle, End };

/** Single line of text with its baseline at y (like SVG text). */
void drawText (juce::Graphics& g, const juce::String& s, float x, float baselineY, const juce::Font& f,
           juce::Colour c, Anchor a = Anchor::Middle);

/** Angles in degrees, 0 = 12 o'clock, clockwise (the faceplate's convention). */
juce::Point<float> polar (float cx, float cy, float r, float deg);
juce::Path arc (float cx, float cy, float r, float a0Deg, float a1Deg);
void strokeRound (juce::Graphics& g, const juce::Path& p, float width);

/** The Pegasus badge, tinted. */
std::unique_ptr<juce::Drawable> pegasus (juce::Colour c);

} // namespace uvp::ui
