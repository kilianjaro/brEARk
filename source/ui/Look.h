#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace breark::ui
{
// The brEARk palette: paper, ink, and one signal colour.
namespace colours
{
inline const juce::Colour paper  { 0xffededed };
inline const juce::Colour ink    { 0xff0a0a0a };
inline const juce::Colour signal { 0xffe10600 };
inline const juce::Colour white  { 0xffffffff };
} // namespace colours

// " · " — JUCE reads plain C++ string literals as Latin-1, so non-ASCII text
// must come from UTF-8 explicitly.
inline const juce::String separator = juce::String::fromUTF8 (" \xc2\xb7 ");

// Space Grotesk's cap height (OS/2 table): 700 of 1000 units per em.
inline constexpr float groteskCapHeight = 0.70f;

//==============================================================================
// The embedded fonts (assets/fonts): Space Grotesk Bold for the big word and
// the mark, Space Mono for everything else. Sizes are point sizes, the same
// numbers as the mockups' CSS pixels. Share it with juce::SharedResourcePointer.
class Fonts
{
public:
    Fonts();

    juce::Font grotesk (float size) const;                // tight tracking, for big words
    juce::Font mono (float size, bool bold = false) const;

    // The largest grotesk size up to maxSize at which text fits into width.
    juce::Font groteskFitting (const juce::String& text, float width, float maxSize) const;

private:
    juce::Typeface::Ptr groteskBold, monoRegular, monoBold;
};

//==============================================================================
// Brutalist controls: rectangles, hard 2 px borders, mono capitals.
class Look final : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    // Paper buttons with ink borders: readable on paper, ink and signal red alike.
    static void stylePaperButton (juce::Button& button);

private:
    juce::SharedResourcePointer<Fonts> fonts;
};

} // namespace breark::ui
