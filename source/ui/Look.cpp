#include "Look.h"

#include <BinaryData.h>

namespace breark::ui
{
Fonts::Fonts()
    : groteskBold (juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskBold_ttf, (size_t) BinaryData::SpaceGroteskBold_ttfSize)),
      monoRegular (juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoRegular_ttf, (size_t) BinaryData::SpaceMonoRegular_ttfSize)),
      monoBold (juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoBold_ttf, (size_t) BinaryData::SpaceMonoBold_ttfSize))
{
}

juce::Font Fonts::grotesk (float size) const
{
    return juce::Font (juce::FontOptions (groteskBold).withPointHeight (size).withKerningFactor (-0.04f));
}

juce::Font Fonts::mono (float size, bool bold) const
{
    return juce::Font (juce::FontOptions (bold ? monoBold : monoRegular).withPointHeight (size));
}

juce::Font Fonts::groteskFitting (const juce::String& text, float width, float maxSize) const
{
    const auto atMax = grotesk (maxSize);
    const auto textWidth = juce::GlyphArrangement::getStringWidth (atMax, text);

    if (textWidth <= width || textWidth <= 0.0f)
        return atMax;

    return grotesk (std::max (8.0f, maxSize * width / textWidth));
}

//==============================================================================
Look::Look()
{
    setColour (juce::TextButton::buttonColourId, colours::paper);
    setColour (juce::TextButton::buttonOnColourId, colours::ink);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::TextButton::textColourOnId, colours::paper);
    setColour (juce::ComboBox::outlineColourId, colours::ink);
}

juce::TextLayout Look::layoutTooltip (const juce::String& text) const
{
    constexpr float maxWidth = 300.0f;

    juce::AttributedString attributed;
    attributed.setWordWrap (juce::AttributedString::byWord);
    attributed.setLineSpacing (2.0f);
    attributed.append (text.upToFirstOccurrenceOf ("\n", false, false), fonts->mono (11.0f, true), colours::ink);

    if (text.containsChar ('\n'))
        attributed.append ("\n" + text.fromFirstOccurrenceOf ("\n", false, false), fonts->mono (11.0f), colours::ink);

    juce::TextLayout layout;
    layout.createLayout (attributed, maxWidth);
    return layout;
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    constexpr int padding = 10;
    const auto layout = layoutTooltip (text);
    const auto width = (int) std::ceil (layout.getWidth()) + 2 * padding;
    const auto height = (int) std::ceil (layout.getHeight()) + 2 * padding;

    // Below and right of the pointer, flipped where the window ends.
    auto x = screenPos.x + 14;
    auto y = screenPos.y + 18;

    if (x + width > parentArea.getRight()) x = screenPos.x - width - 6;
    if (y + height > parentArea.getBottom()) y = screenPos.y - height - 6;

    return juce::Rectangle<int> (x, y, width, height).constrainedWithin (parentArea);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (colours::paper);
    g.fillRect (bounds);
    g.setColour (colours::ink);
    g.drawRect (bounds, 2.0f);
    layoutTooltip (text).draw (g, bounds.reduced (10.0f));
}

void Look::stylePaperButton (juce::Button& button)
{
    button.setColour (juce::TextButton::buttonColourId, colours::paper);
    button.setColour (juce::TextButton::textColourOffId, colours::ink);
    button.setColour (juce::ComboBox::outlineColourId, colours::ink);
    button.setWantsKeyboardFocus (false);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                 bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat();
    auto fill = background;

    if (down)
        fill = fill.contrasting (0.2f);
    else if (highlighted)
        fill = fill.contrasting (0.08f);

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.4f);

    g.setColour (fill);
    g.fillRect (bounds);

    g.setColour (button.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.drawRect (bounds, 2.0f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts->mono (juce::jlimit (9.0f, 12.0f, (float) buttonHeight * 0.36f), true);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto on = button.getToggleState();
    auto colour = button.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.4f);

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawText (button.getButtonText().toUpperCase(), button.getLocalBounds().reduced (6, 0),
                juce::Justification::centred, false);
}

} // namespace breark::ui
