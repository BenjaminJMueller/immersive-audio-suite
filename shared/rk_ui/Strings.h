// Raumklang UI: German is the default language, English can be switched on.
// Texts are written in German in the code; tr() looks up the English version.
#pragma once

#include <juce_core/juce_core.h>

namespace rkui {

enum class Language { German, English };

void setLanguage(Language l);
Language language();

/// Translates a German UI text (UTF-8 literal). Unknown texts are returned unchanged.
juce::String tr(const char* germanUtf8);
juce::String tr(const juce::String& german);

/// UTF-8 literal to juce::String (plain const char* is treated as ASCII by JUCE).
inline juce::String u8(const char* utf8) { return juce::String::fromUTF8(utf8); }

/// Number formatting with the right decimal separator ("3,5 m" vs "3.5 m").
juce::String num(double value, int decimals);

} // namespace rkui
