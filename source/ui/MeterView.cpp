#include "MeterView.h"

namespace breark::ui
{
namespace
{
juce::String db (float value, int decimals = 1)
{
    if (! std::isfinite (value) || value <= -100.0f)
        return "-inf";

    return decimals == 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);
}

juce::String hoursMinutes (double seconds)
{
    const auto minutes = (int) (seconds / 60.0);
    return juce::String (minutes / 60) + ":" + juce::String (minutes % 60).paddedLeft ('0', 2);
}

juce::String minutesSeconds (double seconds)
{
    const auto total = (int) std::max (0.0, seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String times (double factor)
{
    return "x" + juce::String (factor, 2);
}

// All positions of one size, in pixels (the mockups' numbers).
struct Layout
{
    float width, height, margin;
    float markSize, markTop;
    float statusSize, statusTop;
    float wordTop, wordMax;
    float batterySize, batteryTop;
    float dataTop, dataSize, dataLine;
    float factorsTop;
    float coachTop, coachSize, coachWidth;
    float rule;
};

Layout layoutFor (MeterSize size)
{
    if (size == MeterSize::poster)
        return { 960, 600, 36, 22, 26, 13, 66, 100, 250, 80, 300, 404, 22, 20, 486, 552, 14, 590, 3 };

    return { 520, 320, 24, 15, 18, 11, 46, 74, 132, 44, 190, 236, 10.5f, 15, 0, 284, 11, 360, 2 };
}

// Draws grotesk text with its capitals starting exactly at capTop (JUCE
// positions text by ascent, which leaves ~28 % of the size empty above caps).
void drawCaps (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
               float capTop, juce::Justification justification)
{
    const auto top = capTop - (font.getAscent() - groteskCapHeight * font.getHeightInPoints());
    g.setFont (font);
    g.drawText (text, area.withY (top).withHeight (font.getHeight()), justification, false);
}

void drawContent (juce::Graphics& g, const MeterModel& m, const Layout& L, MeterSize size, const Fonts& fonts, juce::Colour fg)
{
    const auto inner = L.width - 2.0f * L.margin;
    g.setColour (fg);

    g.setFont (fonts.grotesk (L.markSize));
    g.drawText ("brEARk.", juce::Rectangle<float> (L.margin, L.markTop, 200.0f, L.markSize * 1.3f), juce::Justification::topLeft, false);

    g.setFont (fonts.mono (L.statusSize, true));
    g.drawText (m.status, juce::Rectangle<float> (L.margin, L.statusTop, inner, L.statusSize * 1.5f), juce::Justification::topLeft, true);

    drawCaps (g, m.word, fonts.groteskFitting (m.word, inner + 8.0f, L.wordMax),
              { L.margin - 6.0f, 0.0f, inner + 12.0f, 0.0f }, L.wordTop, juce::Justification::topLeft);

    drawCaps (g, m.battery, fonts.grotesk (L.batterySize), { L.margin, 0.0f, inner, 0.0f }, L.batteryTop, juce::Justification::topRight);

    if (size == MeterSize::compact)
    {
        g.setFont (fonts.mono (L.dataSize));

        for (int i = 0; i < m.compactLines.size(); ++i)
            g.drawText (m.compactLines[i], juce::Rectangle<float> (L.margin, L.dataTop + (float) i * L.dataLine, inner, L.dataLine),
                        juce::Justification::centredLeft, true);
    }
    else
    {
        // Six columns of stats under a rule, then the drain factors.
        g.fillRect (juce::Rectangle<float> (L.margin, L.dataTop, inner, L.rule));
        const auto columnWidth = (inner - 5.0f * 12.0f) / 6.0f;

        for (int i = 0; i < std::min (6, m.statLabels.size()); ++i)
        {
            const auto x = L.margin + (float) i * (columnWidth + 12.0f);
            g.setFont (fonts.mono (10.0f));
            g.drawText (m.statLabels[i], juce::Rectangle<float> (x, L.dataTop + 12.0f, columnWidth, 14.0f), juce::Justification::centredLeft, true);
            g.setFont (fonts.mono (L.dataSize, true));
            g.drawText (m.statValues[i], juce::Rectangle<float> (x, L.dataTop + 28.0f, columnWidth, 30.0f), juce::Justification::centredLeft, true);
        }

        g.setFont (fonts.mono (11.0f));
        g.drawText (m.factors, juce::Rectangle<float> (L.margin, L.factorsTop, inner, 16.0f), juce::Justification::centredLeft, true);
    }

    g.setFont (fonts.mono (L.coachSize));
    g.drawText ("> " + m.coach, juce::Rectangle<float> (L.margin, L.coachTop, L.coachWidth, L.coachSize * 1.5f),
                juce::Justification::centredLeft, true);
}
} // namespace

//==============================================================================
juce::Rectangle<int> meterBounds (MeterSize size)
{
    const auto L = layoutFor (size);
    return { (int) L.width, (int) L.height };
}

MeterModel makeMeterModel (const Snapshot& s, const juce::String& coachLine, bool activeInstance, PowerDown::Stage powerStage)
{
    MeterModel m;
    m.coach = coachLine;

    const auto battery = juce::jlimit (0.0, 100.0, (double) s.battery);
    m.batteryFraction = battery / 100.0;
    m.battery = juce::String (juce::roundToInt (battery)) + " %";

    const auto minutes = juce::String (juce::roundToInt (s.suggestedBreakMinutes));
    const auto ignored = s.secondsSinceDue >= model::firstReminderSeconds + model::reminderIntervalSeconds; // the brutal stage

    if (! activeInstance)
    {
        m.word = "INACTIVE.";
        m.status = "ANOTHER brEARk IS ALREADY LISTENING";
    }
    else if (s.hostOff)
    {
        m.word = "OFF.";
        m.status = "SWITCHED OFF IN LIVE" + separator + "NOT COUNTING";
    }
    else if (s.breakPhase == BreakPhase::unplugged)
    {
        m.word = "UNPLUGGED.";
        m.alarm = true;
        m.status = "UNPLUGGED" + separator + minutesSeconds (s.unpluggedSeconds) + " OF " + minutesSeconds (model::minimumBreakSeconds);
    }
    else if (s.breakPhase == BreakPhase::onBreak)
    {
        m.word = "RESTING.";
        m.status = "ON A BREAK" + separator + minutesSeconds (s.breakSecondsLeft) + " LEFT";
    }
    else if (s.breakPhase == BreakPhase::due)
    {
        m.word = ignored ? "STOP." : "BREAK.";
        m.alarm = ignored;
        m.status = ignored ? "BREAK DUE" + separator + minutesSeconds (s.secondsSinceDue) + " OVER" : "BREAK DUE" + separator + minutes + " MIN";
    }
    else if (s.recovering)
    {
        m.word = "RESTING.";
        m.status = "SILENCE" + separator + "RECOVERING";
    }
    else
    {
        m.word = battery > 70.0 ? "FRESH." : (battery > 40.0 ? "OK." : "TIRING.");
    }

    // Status details that override or extend the line.
    if (powerStage == PowerDown::Stage::spinningDown) m.status = "POWERING DOWN";
    if (powerStage == PowerDown::Stage::spinningUp)   m.status = "BACK ON";

    if (s.snoozeSecondsLeft > 0.0 && s.breakPhase != BreakPhase::unplugged)
        m.status = "SNOOZED" + separator + minutesSeconds (s.snoozeSecondsLeft) + " LEFT";

    if (s.rendering && ! s.hostOff)
        m.status = "EXPORT" + separator + "NOT COUNTING";

    if (s.timeWarp > 1)
        m.status = m.status.isEmpty() ? "TEST MODE x" + juce::String (s.timeWarp) + separator + "NOT SAVED"
                                      : m.status + separator + "TEST x" + juce::String (s.timeWarp);

    m.massRed = battery <= model::warnBattery && ! m.alarm;

    // The numbers.
    const auto level = std::isfinite (s.perceivedSpl) ? "~" + db (s.perceivedSpl, 0) + " dB" : juce::String ("-");
    const auto psr = s.silent ? juce::String ("-") : db (s.psr);
    const auto& f = s.factors;
    const auto loop = s.loopBars > 0 ? juce::String (s.loopBars) + " BARS " + minutesSeconds (s.loopingSeconds)
                    : (s.barPlays >= 10.0f ? juce::String (juce::roundToInt (s.barPlays)) + " BAR PLAYS" : juce::String ("NO LOOP"));

    m.compactLines.add ("DOSE " + juce::String (juce::roundToInt (s.doseToday)) + " %   LISTENED " + hoursMinutes (s.listeningSecondsToday)
                        + "   SINCE BREAK " + hoursMinutes (s.secondsSinceBreak));
    m.compactLines.add (db (s.lufsShortTerm) + " LUFS   " + level + "   PSR " + psr + "   DRAIN " + times (s.drainFactor));

    m.statLabels.addArray ({ "DOSE TODAY", "WEEK (WHO)", "LISTENED", "SINCE BREAK", "LEVEL", "LOUDNESS" });
    m.statValues.addArray ({ juce::String (juce::roundToInt (s.doseToday)) + " %",
                             juce::String (juce::roundToInt (s.doseWeek)) + " %",
                             hoursMinutes (s.listeningSecondsToday),
                             hoursMinutes (s.secondsSinceBreak),
                             level,
                             db (s.lufsShortTerm) });

    m.factors = "HARSH " + times (f.harshness) + "   SQUASHED " + times (f.dynamics) + " (PSR " + psr + ")   LOOP "
              + times (f.repetition) + " (" + loop + ")   NIGHT " + times (f.night) + "   =  DRAIN " + times (s.drainFactor);

    return m;
}

void paintMeter (juce::Graphics& g, const MeterModel& m, MeterSize size, const Fonts& fonts)
{
    const auto L = layoutFor (size);

    const auto baseBackground = m.alarm ? colours::signal : colours::paper;
    const auto baseText = m.alarm ? colours::white : colours::ink;
    const auto massBackground = m.massRed ? colours::signal : colours::ink;
    const auto massText = m.massRed ? colours::white : colours::paper;

    g.fillAll (baseBackground);
    drawContent (g, m, L, size, fonts, baseText);

    // The mass: the same content again, clipped to the battery level, inverted.
    const auto massHeight = juce::roundToInt (L.height * m.batteryFraction);

    if (massHeight > 0)
    {
        const juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, (int) L.height - massHeight, (int) L.width, massHeight));
        g.fillAll (massBackground);
        drawContent (g, m, L, size, fonts, massText);
    }
}

} // namespace breark::ui
