#include "HistoryView.h"

namespace breark::ui
{
juce::String weekdayName (int day)
{
    static const char* names[] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
    return names[((day + 3) % 7 + 7) % 7];
}

namespace
{
juce::String clockFor (int minuteOfDay)
{
    const auto total = (minuteOfDay + model::dayStartHour * 60) % (24 * 60);
    return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

void sectionTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, const Fonts& fonts)
{
    g.fillRect (area.removeFromTop (3.0f));
    g.setFont (fonts.mono (11.0f, true));
    g.drawText (text, area.withTrimmedTop (8.0f), juce::Justification::topLeft, false);
}
} // namespace

void paintHistory (juce::Graphics& g, juce::Rectangle<float> bounds, const Brain::History& history, const Fonts& fonts)
{
    g.setColour (colours::paper);
    g.fillRect (bounds);
    g.setColour (colours::ink);

    auto area = bounds.reduced (36.0f, 26.0f);

    g.setFont (fonts.grotesk (44.0f));
    g.drawText ("HISTORY.", area.removeFromTop (56.0f), juce::Justification::centredLeft, false);
    area.removeFromTop (10.0f);

    //── Today ────────────────────────────────────────────────────────────────
    sectionTitle (g, area.removeFromTop (30.0f), "TODAY" + separator + "EAR BATTERY", fonts);
    auto chart = area.removeFromTop (170.0f).reduced (0.0f, 8.0f);
    auto labels = chart.removeFromBottom (16.0f);

    const auto first = history.today.empty() ? history.nowMinute : history.today.front().minute;
    const auto last = std::max (history.nowMinute, first + 60);
    auto xFor = [&] (int minute) { return chart.getX() + chart.getWidth() * (float) (minute - first) / (float) (last - first); };
    auto yFor = [&] (int battery) { return chart.getBottom() - chart.getHeight() * (float) battery / 100.0f; };

    const float dashes[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ chart.getX(), yFor (20), chart.getRight(), yFor (20) }, dashes, 2, 1.0f);
    g.setFont (fonts.mono (10.0f));
    g.drawText ("20 %", juce::Rectangle<float> (chart.getRight() - 60.0f, yFor (20) - 16.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    juce::Path line;
    auto previous = -1000;

    for (const auto& point : history.today)
    {
        const auto x = xFor (point.minute), y = yFor (point.battery);

        if (point.minute - previous > 5) // Live was closed: leave a gap
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);

        previous = point.minute;
    }

    g.strokePath (line, juce::PathStrokeType (3.0f, juce::PathStrokeType::mitered));

    if (history.today.empty())
        g.drawText ("NO LISTENING YET TODAY", chart, juce::Justification::centred, false);

    g.drawText (clockFor (first), labels, juce::Justification::centredLeft, false);
    g.drawText ("NOW " + clockFor (history.nowMinute), labels, juce::Justification::centredRight, false);

    //── Last 7 days ──────────────────────────────────────────────────────────
    area.removeFromTop (8.0f);
    sectionTitle (g, area.removeFromTop (30.0f), "LAST 7 DAYS" + separator + "DAILY DOSE (WHO)", fonts);

    // Always the last 7 calendar days; a day without listening is 0 %.
    auto bars = area.removeFromTop (160.0f);
    const auto gap = 16.0f;
    const auto barWidth = (bars.getWidth() - 6.0f * gap) / 7.0f;
    const auto today = history.lastDays.empty() ? 0 : history.lastDays.back().day;

    for (int i = 0; i < 7; ++i)
    {
        const auto day = today - 6 + i;
        double dose = 0.0;

        for (const auto& record : history.lastDays)
            if (record.day == day)
                dose = record.dose;

        auto column = juce::Rectangle<float> (bars.getX() + (float) i * (barWidth + gap), bars.getY(), barWidth, bars.getHeight());
        const auto name = column.removeFromBottom (16.0f);
        const auto barHeight = juce::jlimit (0.0f, column.getHeight() - 18.0f, (float) dose * 1.2f);

        g.setColour (dose >= 100.0 ? colours::signal : colours::ink);
        g.fillRect (column.removeFromBottom (barHeight));

        g.setColour (colours::ink);
        g.setFont (fonts.mono (11.0f, true));
        g.drawText (juce::String (juce::roundToInt (dose)) + " %", column.removeFromBottom (18.0f), juce::Justification::centredLeft, false);
        g.setFont (fonts.mono (10.0f));
        g.drawText (i == 6 ? juce::String ("TODAY") : weekdayName (day), name, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
HistoryOverlay::HistoryOverlay()
{
    setOpaque (true);
    closeButton.setColour (juce::TextButton::buttonColourId, colours::ink);
    closeButton.setColour (juce::TextButton::textColourOffId, colours::paper);
    closeButton.setColour (juce::ComboBox::outlineColourId, colours::ink);
    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);
}

void HistoryOverlay::setHistory (Brain::History newHistory)
{
    history = std::move (newHistory);
    repaint();
}

void HistoryOverlay::resized()
{
    closeButton.setBounds (getWidth() - 32 - 110, 30, 110, 40);
}

void HistoryOverlay::paint (juce::Graphics& g)
{
    paintHistory (g, getLocalBounds().toFloat(), history, *fonts);
}

} // namespace breark::ui
