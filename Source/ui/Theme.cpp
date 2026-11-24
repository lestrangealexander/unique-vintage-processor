// Unique Vintage Processor — faceplate v4 theme
#include "Theme.h"
#include "BinaryData.h"

namespace uvp::ui
{
namespace
{
    juce::Typeface::Ptr face (Weight w)
    {
        static const juce::Typeface::Ptr faces[3] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::DMSansMedium_ttf, (size_t) BinaryData::DMSansMedium_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::DMSansSemiBold_ttf, (size_t) BinaryData::DMSansSemiBold_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::DMSansBold_ttf, (size_t) BinaryData::DMSansBold_ttfSize),
        };
        return faces[(int) w];
    }
} // namespace

juce::Font font (Weight w, float sizePx, float letterSpacingPx)
{
    juce::Font f (juce::FontOptions (face (w)).withPointHeight (sizePx));
    if (letterSpacingPx != 0.0f && f.getHeight() > 0.0f)
        f = f.withExtraKerningFactor (letterSpacingPx / f.getHeight());
    return f;
}

float textWidth (const juce::Font& f, const juce::String& s)
{
    // letter-spacing also follows the last glyph; leave it out when measuring so centring matches the mockup
    float w = juce::GlyphArrangement::getStringWidth (f, s);
    if (s.isNotEmpty()) w -= f.getExtraKerningFactor() * f.getHeight();
    return w;
}

void drawText (juce::Graphics& g, const juce::String& s, float x, float baselineY, const juce::Font& f, juce::Colour c, Anchor a)
{
    g.setColour (c);
    g.setFont (f);
    const float w = textWidth (f, s);
    float x0 = x;
    if (a == Anchor::Middle) x0 = x - 0.5f * w;
    else if (a == Anchor::End) x0 = x - w;
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, x0, baselineY);
    ga.draw (g);
}

juce::Point<float> polar (float cx, float cy, float r, float deg)
{
    const float a = juce::degreesToRadians (deg);
    return { cx + r * std::sin (a), cy - r * std::cos (a) };
}

juce::Path arc (float cx, float cy, float r, float a0Deg, float a1Deg)
{
    juce::Path p;
    p.addCentredArc (cx, cy, r, r, 0.0f, juce::degreesToRadians (a0Deg), juce::degreesToRadians (a1Deg), true);
    return p;
}

void strokeRound (juce::Graphics& g, const juce::Path& p, float width)
{
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

std::unique_ptr<juce::Drawable> pegasus (juce::Colour c)
{
    auto d = juce::Drawable::createFromImageData (BinaryData::Pegasus_svg, (size_t) BinaryData::Pegasus_svgSize);
    if (d != nullptr)
        d->replaceColour (juce::Colours::black, c);
    return d;
}

} // namespace uvp::ui
