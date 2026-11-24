// Unique Vintage Processor — reading typed-in values ("4k", "-3 dB", "50 %", "off")
#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace uvp
{

/** The number in a typed value. Units are optional; for frequencies "k" / "kHz" means thousands and a
    bare number under 20 is read as kHz ("4" -> 4 kHz, "250" -> 250 Hz). "off" gives offValue when the
    parameter has one. Locale-independent (accepts "4,5" as 4.5 and the Unicode minus sign). */
inline std::optional<float> parseTypedValue (juce::String text, bool isFrequency, std::optional<float> offValue = {})
{
    text = text.trim().toLowerCase()
               .replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-")
               .replace (",", ".");
    if (offValue.has_value() && text.startsWith ("off"))
        return offValue;

    const auto s = text.toStdString();
    const size_t n = s.size();
    auto isDigit = [] (char c) { return c >= '0' && c <= '9'; };
    size_t i = 0;
    while (i < n && ! (isDigit (s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.'))
        ++i;
    double sign = 1.0;
    if (i < n && (s[i] == '-' || s[i] == '+'))
    {
        if (s[i] == '-') sign = -1.0;
        ++i;
    }
    double whole = 0.0, frac = 0.0, scale = 1.0;
    bool anyDigits = false;
    while (i < n && isDigit (s[i])) { whole = whole * 10.0 + (s[i] - '0'); ++i; anyDigits = true; }
    if (i < n && s[i] == '.')
    {
        ++i;
        while (i < n && isDigit (s[i])) { frac = frac * 10.0 + (s[i] - '0'); scale *= 10.0; ++i; anyDigits = true; }
    }
    if (! anyDigits)
        return std::nullopt;

    double value = sign * (whole + frac / scale);
    const auto unit = juce::String (s.substr (i)).trim();
    if (isFrequency)
    {
        if (unit.startsWith ("k"))
            value *= 1000.0;
        else if (! unit.startsWith ("hz") && std::abs (value) < 20.0)
            value *= 1000.0;
    }
    if (! std::isfinite (value))
        return std::nullopt;
    return (float) value;
}

} // namespace uvp
