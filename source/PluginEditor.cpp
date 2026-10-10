#include "PluginEditor.h"

namespace
{
const auto paper = juce::Colour (0xffededed);
const auto ink   = juce::Colour (0xff0a0a0a);
} // namespace

//==============================================================================
BrearkEditor::BrearkEditor (BrearkProcessor& p)
    : AudioProcessorEditor (p)
{
    setSize (520, 320);
}

void BrearkEditor::paint (juce::Graphics& g)
{
    g.fillAll (paper);
    g.setColour (ink);

    auto area = getLocalBounds().reduced (24);

    g.setFont (juce::Font (juce::FontOptions (88.0f, juce::Font::bold)));
    g.drawText ("brEARk.", area.removeFromTop (96), juce::Justification::topLeft, false);

    g.fillRect (area.removeFromTop (4));
    area.removeFromTop (14);

    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));

    const juce::StringArray rows {
        "MILESTONE     M0 / PASS-THROUGH",
        "AUDIO PATH    BIT-TRANSPARENT",
        "LATENCY       " + juce::String (processor.getLatencySamples()) + " SAMPLES",
        "FORMAT        " + wrapper.toUpperCase(),
        "PRECISION     " + juce::String (processor.isUsingDoublePrecision() ? "64-BIT FLOAT" : "32-BIT FLOAT"),
        "SAMPLE RATE   " + juce::String (processor.getSampleRate(), 0) + " HZ",
    };

    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

    for (const auto& row : rows)
        g.drawText (row, area.removeFromTop (19), juce::Justification::centredLeft, false);

    g.drawText ("> Nothing to say yet. I'm only listening.",
                area.removeFromBottom (19), juce::Justification::centredLeft, false);
}
