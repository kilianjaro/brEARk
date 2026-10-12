#pragma once

#include "Look.h"
#include "engine/Brain.h"

namespace breark::ui
{
// HISTORY.: today's ear battery as a line (gaps where Live was closed, a
// dashed line at 20 %), and the daily dose of the last 7 days as bars (red at
// 100 % and above). Drawn into bounds, ink on paper.
void paintHistory (juce::Graphics& g, juce::Rectangle<float> bounds, const Brain::History& history, const Fonts& fonts);

// The explanation for what's under the mouse (the curve, a day's bar), or empty.
juce::String historyTooltipAt (juce::Rectangle<float> bounds, const Brain::History& history, juce::Point<float> position);

// "MON" … "SUN" for a day number (days since 1970-01-01, a Thursday).
juce::String weekdayName (int day);

//==============================================================================
// The history as an overlay over the POSTER view, with a CLOSE button.
class HistoryOverlay final : public juce::Component,
                              public juce::TooltipClient
{
public:
    HistoryOverlay();

    juce::String getTooltip() override;

    void setHistory (Brain::History newHistory);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    Brain::History history;
    juce::TextButton closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistoryOverlay)
};

} // namespace breark::ui
