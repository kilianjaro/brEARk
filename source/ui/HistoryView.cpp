#include "HistoryView.h"
#include "Tooltips.h"

namespace breark::ui
{
juce::String weekdayName (int day)
{
    static const char* names[] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
    return names[((day + 3) % 7 + 7) % 7];
}

juce::String describeWeek (const WeekSummary& w)
{
    auto clock = [] (double seconds)
    {
        const auto minutes = (int) (seconds / 60.0);
        return juce::String (minutes / 60) + ":" + juce::String (minutes % 60).paddedLeft ('0', 2);
    };

    auto text = juce::String (juce::roundToInt (w.whoWeekPercent)) + " % of the weekly hearing dose used";

    if (w.daysOverDose > 0)
        text << ", " << w.daysOverDose << (w.daysOverDose == 1 ? " day" : " days") << " used up";

    text << ". " << clock (w.listeningSeconds) << " listened on " << w.daysListened << (w.daysListened == 1 ? " day" : " days")
         << ", longest stretch " << clock (w.longestStretchSeconds) << ". Battery ran empty " << w.emptyBattery
         << (w.emptyBattery == 1 ? " time, " : " times, ") << w.breaks << (w.breaks == 1 ? " break." : " breaks.");
    return text;
}

namespace
{
juce::String hoursMinutes (double seconds)
{
    const auto minutes = (int) (seconds / 60.0);
    return juce::String (minutes / 60) + ":" + juce::String (minutes % 60).paddedLeft ('0', 2);
}

juce::String clockFor (int minuteOfDay)
{
    const auto total = (minuteOfDay + model::dayStartHour * 60) % (24 * 60);
    return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

// Where everything sits, shared by drawing and tooltips.
struct HistoryLayout
{
    juce::Rectangle<float> title, todayTitle, chart, chartLabels, weekTitle, weeks;
    std::array<juce::Rectangle<float>, 7> days; // the full column of each day
};

HistoryLayout layoutHistory (juce::Rectangle<float> bounds)
{
    HistoryLayout l;
    auto area = bounds.reduced (36.0f, 26.0f);
    l.title = area.removeFromTop (56.0f);
    area.removeFromTop (10.0f);
    l.todayTitle = area.removeFromTop (30.0f);
    l.chart = area.removeFromTop (170.0f).reduced (0.0f, 8.0f);
    l.chartLabels = l.chart.removeFromBottom (16.0f);
    area.removeFromTop (8.0f);
    l.weekTitle = area.removeFromTop (30.0f);

    const auto bars = area.removeFromTop (160.0f);
    const auto gap = 16.0f;
    const auto barWidth = (bars.getWidth() - 6.0f * gap) / 7.0f;

    for (int i = 0; i < 7; ++i)
        l.days[(size_t) i] = { bars.getX() + (float) i * (barWidth + gap), bars.getY(), barWidth, bars.getHeight() };

    area.removeFromTop (6.0f);
    l.weeks = area;
    return l;
}

const DayRecord* recordFor (const Brain::History& history, int day)
{
    for (const auto& record : history.lastDays)
        if (record.day == day)
            return &record;

    return nullptr;
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

    const auto l = layoutHistory (bounds);

    g.setFont (fonts.grotesk (44.0f));
    g.drawText ("HISTORY.", l.title, juce::Justification::centredLeft, false);

    //── Today ────────────────────────────────────────────────────────────────
    const auto offToday = history.lastDays.empty() ? 0.0 : history.lastDays.back().offSeconds;
    sectionTitle (g, l.todayTitle,
                  "TODAY" + separator + "EAR BATTERY" + (offToday >= 60.0 ? separator + "OFF " + hoursMinutes (offToday) : juce::String()),
                  fonts);
    const auto chart = l.chart;
    const auto labels = l.chartLabels;

    const auto first = history.today.empty() ? history.nowMinute : history.today.front().minute;
    const auto last = std::max (history.nowMinute, first + 60);
    auto xFor = [&] (int minute) { return chart.getX() + chart.getWidth() * (float) (minute - first) / (float) (last - first); };
    auto yFor = [&] (int battery) { return chart.getBottom() - chart.getHeight() * (float) battery / 100.0f; };

    const float dashes[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ chart.getX(), yFor (20), chart.getRight(), yFor (20) }, dashes, 2, 1.0f);
    g.setFont (fonts.mono (10.0f));
    g.drawText ("20 %", juce::Rectangle<float> (chart.getRight() - 60.0f, yFor (20) - 16.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    // Minutes brEARk was switched off in Live: hatched, labelled OFF.
    for (size_t i = 0; i < history.today.size();)
    {
        if (! history.today[i].off)
        {
            ++i;
            continue;
        }

        auto end = i;

        while (end + 1 < history.today.size() && history.today[end + 1].off && history.today[end + 1].minute - history.today[end].minute <= 5)
            ++end;

        const auto band = juce::Rectangle<float>::leftTopRightBottom (xFor (history.today[i].minute), chart.getY(),
                                                                     std::max (xFor (history.today[end].minute + 1), xFor (history.today[i].minute) + 2.0f),
                                                                     chart.getBottom());
        {
            const juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (band.toNearestInt());

            for (auto x = band.getX() - band.getHeight(); x < band.getRight(); x += 6.0f)
                g.drawLine (x, band.getBottom(), x + band.getHeight(), band.getY(), 1.0f);
        }

        if (band.getWidth() >= 40.0f)
        {
            g.setFont (fonts.mono (10.0f, true));
            const auto label = band.withSizeKeepingCentre (34.0f, 16.0f);
            g.setColour (colours::paper);
            g.fillRect (label);
            g.setColour (colours::ink);
            g.drawText ("OFF", label, juce::Justification::centred, false);
        }

        i = end + 1;
    }

    juce::Path line;
    auto previous = -1000;

    for (const auto& point : history.today)
    {
        if (point.off) // switched off: no battery reading, leave a gap
        {
            previous = -1000;
            continue;
        }

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
    sectionTitle (g, l.weekTitle, "LAST 7 DAYS" + separator + "HEARING DOSE USED (WHO)", fonts);

    // Always the last 7 calendar days; a day without listening is 0 %.
    const auto today = history.lastDays.empty() ? 0 : history.lastDays.back().day;

    for (int i = 0; i < 7; ++i)
    {
        const auto day = today - 6 + i;
        double dose = 0.0;

        for (const auto& record : history.lastDays)
            if (record.day == day)
                dose = record.dose;

        double offSeconds = 0.0;

        for (const auto& record : history.lastDays)
            if (record.day == day)
                offSeconds = record.offSeconds;

        auto column = l.days[(size_t) i];
        const auto name = column.removeFromBottom (16.0f);
        const auto barHeight = juce::jlimit (0.0f, column.getHeight() - 18.0f, (float) dose * 1.2f);

        g.setColour (dose >= 100.0 ? colours::signal : colours::ink);
        g.fillRect (column.removeFromBottom (barHeight));

        g.setColour (colours::ink);
        g.setFont (fonts.mono (11.0f, true));
        g.drawText (juce::String (juce::roundToInt (dose)) + " %", column.removeFromBottom (18.0f), juce::Justification::centredLeft, false);
        g.setFont (fonts.mono (10.0f));
        g.drawText ((i == 6 ? juce::String ("TODAY") : weekdayName (day))
                        + (offSeconds >= 60.0 ? separator + "OFF " + hoursMinutes (offSeconds) : juce::String()),
                    name, juce::Justification::centredLeft, false);
    }

    //── This week, last week (M10) ───────────────────────────────────────────
    auto weeksArea = l.weeks;
    g.fillRect (weeksArea.removeFromTop (2.0f));
    weeksArea.removeFromTop (6.0f);

    for (const auto& [label, week] : { std::pair { juce::String ("THIS WEEK"), history.thisWeek },
                                       std::pair { juce::String ("LAST WEEK"), history.lastWeek } })
    {
        auto row = weeksArea.removeFromTop (34.0f);
        g.setFont (fonts.mono (11.0f, true));
        g.drawText (label, row.removeFromLeft (100.0f), juce::Justification::topLeft, false);
        g.setFont (fonts.mono (11.0f));
        g.drawFittedText (week.daysListened > 0 ? describeWeek (week) : juce::String ("Nothing measured."),
                          row.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
}

juce::String historyTooltipAt (juce::Rectangle<float> bounds, const Brain::History& history, juce::Point<float> p)
{
    const auto l = layoutHistory (bounds);

    if (l.chart.getUnion (l.todayTitle).contains (p))
        return tips::todayCurve;

    if (l.weeks.contains (p))
        return tips::weeks;

    const auto today = history.lastDays.empty() ? 0 : history.lastDays.back().day;

    for (int i = 0; i < 7; ++i)
    {
        if (! l.days[(size_t) i].contains (p))
            continue;

        const auto day = today - 6 + i;
        const auto* record = recordFor (history, day);
        return tips::day (i == 6 ? juce::String ("TODAY") : weekdayName (day), record != nullptr ? record->dose : 0.0,
                          record != nullptr ? record->listeningSeconds : 0.0, record != nullptr ? record->breaks : 0,
                          record != nullptr ? record->offSeconds : 0.0);
    }

    return {};
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
    closeButton.setTooltip (tips::close);
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

juce::String HistoryOverlay::getTooltip()
{
    return historyTooltipAt (getLocalBounds().toFloat(), history, getMouseXYRelative().toFloat());
}

void HistoryOverlay::paint (juce::Graphics& g)
{
    paintHistory (g, getLocalBounds().toFloat(), history, *fonts);
}

} // namespace breark::ui
