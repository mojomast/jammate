// Font seam for JUCE component tests; the application embeds its fonts in the
// editor. Tests use system fonts and do not claim production screenshot fidelity.
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    return juce::Typeface::createSystemTypefaceFor (juce::Font (
        juce::FontOptions ("DejaVu Sans", 14.0f, bold ? juce::Font::bold : juce::Font::plain)));
}
juce::Typeface::Ptr monoTypeface (bool bold)
{
    return juce::Typeface::createSystemTypefaceFor (juce::Font (
        juce::FontOptions ("DejaVu Sans Mono", 14.0f, bold ? juce::Font::bold : juce::Font::plain)));
}
}
