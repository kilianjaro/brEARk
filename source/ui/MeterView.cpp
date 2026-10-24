#include "MeterView.h"
#include "Tooltips.h"

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
    float timeTop, timeSize;
    float coachTop, coachSize, coachWidth;
    float rule;
};

Layout layoutFor (MeterSize size)
{
    if (size == MeterSize::poster)
        return { 960, 600, 36, 22, 26, 13, 66, 100, 250, 80, 300, 404, 22, 20, 486, 370, 14, 546, 14, 590, 3 };

    return { 520, 320, 24, 15, 18, 11, 46, 74, 132, 44, 190, 245, 10.5f, 15, 0, 229, 10.5f, 280, 11, 340, 2 };
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

// Items in one mono line, three spaces apart: where each one sits.
std::vector<juce::Rectangle<float>> itemBounds (const std::vector<Item>& items, const juce::Font& font, juce::Rectangle<float> line)
{
    std::vector<juce::Rectangle<float>> result;
    const auto gap = juce::GlyphArrangement::getStringWidth (font, "   ");
    auto x = line.getX();

    for (const auto& item : items)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (font, item.text);
        result.push_back ({ x, line.getY(), width, line.getHeight() });
        x += width + gap;
    }

    return result;
}

juce::Rectangle<float> compactLine (const Layout& L, size_t index)
{
    return { L.margin, L.dataTop + (float) index * L.dataLine, L.width - 2.0f * L.margin, L.dataLine };
}

constexpr size_t statCount = 5; // POSTER: hearing dose today and this week, listened, since break, signal

juce::Rectangle<float> statColumn (const Layout& L, size_t index)
{
    const auto inner = L.width - 2.0f * L.margin;
    const auto columnWidth = (inner - (float) (statCount - 1) * 12.0f) / (float) statCount;
    return { L.margin + (float) index * (columnWidth + 12.0f), L.dataTop + 12.0f, columnWidth, 46.0f };
}

juce::Rectangle<float> factorsLine (const Layout& L)
{
    return { L.margin, L.factorsTop, L.width - 2.0f * L.margin, 16.0f };
}

juce::Rectangle<float> timeLine (const Layout& L)
{
    return { L.margin, L.timeTop, L.width - 2.0f * L.margin, L.timeSize * 1.4f };
}

juce::Rectangle<float> coachArea (const Layout& L)
{
    return { L.margin, L.coachTop, L.coachWidth, L.coachSize * 2.8f }; // two lines
}

void drawItems (juce::Graphics& g, const std::vector<Item>& items, const juce::Font& font, juce::Rectangle<float> line)
{
    g.setFont (font);
    const auto bounds = itemBounds (items, font, line);

    for (size_t i = 0; i < items.size(); ++i)
        g.drawText (items[i].text, bounds[i].withRight (std::max (bounds[i].getRight(), line.getRight())), juce::Justification::centredLeft, true);
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

    drawItems (g, m.timeLine, fonts.mono (L.timeSize, true), timeLine (L));

    if (size == MeterSize::compact)
    {
        for (size_t i = 0; i < m.compactLines.size(); ++i)
            drawItems (g, m.compactLines[i], fonts.mono (L.dataSize), compactLine (L, i));
    }
    else
    {
        // Six columns of stats under a rule, then why the battery drains.
        g.fillRect (juce::Rectangle<float> (L.margin, L.dataTop, inner, L.rule));

        for (size_t i = 0; i < std::min (statCount, m.stats.size()); ++i)
        {
            auto column = statColumn (L, i);
            g.setFont (fonts.mono (10.0f));
            g.drawText (m.stats[i].label, column.removeFromTop (16.0f), juce::Justification::centredLeft, true);
            g.setFont (fonts.mono (L.dataSize, true));
            g.drawText (m.stats[i].value, column, juce::Justification::centredLeft, true);
        }

        drawItems (g, m.factors, fonts.mono (11.0f), factorsLine (L));
    }

    // The coach: up to two lines (lines are at most 104 characters).
    g.setFont (fonts.mono (L.coachSize));
    g.drawFittedText ("> " + m.coach, coachArea (L).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
}
} // namespace

//==============================================================================
juce::Rectangle<int> meterBounds (MeterSize size)
{
    const auto L = layoutFor (size);
    return { (int) L.width, (int) L.height };
}

juce::Rectangle<int> levelSwitchArea (MeterSize size)
{
    const auto L = layoutFor (size);

    // POSTER: at the end of the time line. COMPACT: right of the data lines,
    // above BREAK.
    if (size == MeterSize::poster)
        return juce::Rectangle<float> (L.width - L.margin - 270.0f, L.timeTop - 5.0f, 270.0f, 28.0f).toNearestInt();

    return juce::Rectangle<float> (L.width - L.margin - 180.0f, L.dataTop - 2.0f, 180.0f, 22.0f).toNearestInt();
}

MeterModel makeMeterModel (const Snapshot& s, const juce::String& coachLine, bool activeInstance, PowerDown::Stage powerStage)
{
    MeterModel m;
    m.coach = coachLine;

    const auto battery = juce::jlimit (0.0, 100.0, (double) s.battery);
    m.batteryFraction = battery / 100.0;
    m.battery = juce::String (juce::roundToInt (battery)) + " %";

    const auto minutes = juce::String (juce::roundToInt (s.suggestedBreakMinutes));
    const auto ignored = s.secondsSinceDue >= model::stopSeconds; // the brutal stage

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
    else if (s.unpluggedForDay)
    {
        m.word = "DONE.";
        m.alarm = true;
        m.status = "UNPLUGGED UNTIL 05:00" + separator + "SNOOZE 10 MIN IF YOU MUST";
    }
    else if (s.dayDone)
    {
        m.word = "DONE.";
        m.status = "HEARING DOSE USED UP" + separator + "CALL IT A DAY";
    }
    else if (s.breakPhase == BreakPhase::unplugged)
    {
        m.word = "UNPLUGGED.";
        m.alarm = true;
        m.status = "SOUND BACK IN " + minutesSeconds (std::max (0.0, model::minimumBreakSeconds - s.unpluggedSeconds));
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
        m.status = ignored ? "EAR BATTERY EMPTY" + separator + "BREAK OVERDUE " + minutesSeconds (s.secondsSinceDue) : juce::String ("EAR BATTERY EMPTY");
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

    // Time left, and what one level quieter would buy (M10/M11). M11.2: one
    // line, one job: the status line says what's happening, this line only
    // gives times, and neither repeats the other.
    auto approx = [] (double forecastMinutes) -> juce::String
    {
        if (! std::isfinite (forecastMinutes) || forecastMinutes >= 600.0)
            return "10 H+";

        const auto total = std::max (1, juce::roundToInt (forecastMinutes));
        return total < 60 ? "~" + juce::String (total) + " MIN"
                          : "~" + juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2) + " H";
    };

    const auto playing = ! s.silent && s.breakPhase != BreakPhase::unplugged; // unplugged: you hear nothing
    const auto levelName = juce::String (s.settings.preset == LevelPreset::quiet ? "QUIET" : s.settings.preset == LevelPreset::loud ? "LOUD" : "NORMAL");
    const auto lowerName = juce::String (s.settings.preset == LevelPreset::loud ? "NORMAL" : "QUIET");

    if (! activeInstance || s.hostOff)
        m.timeLine = {}; // nothing runs, nothing to time
    else if (s.breakPhase == BreakPhase::due && playing)
        m.timeLine = { { "TAKE ~" + minutes + " MIN OF QUIET", tips::breakLength } };
    else if (! playing)
        m.timeLine = { { battery < model::suggestedBreakTargetBattery ? "90 % IN " + approx (s.minutesToRecover)
                                                                      : juce::String ("RESTED"), tips::recover } };
    else
    {
        m.timeLine = { { "BREAK IN " + approx (s.minutesToBreak), tips::timeLeft } };

        if (std::isfinite (s.minutesToBreakQuieter))
            m.timeLine.push_back ({ "AT " + lowerName + " " + approx (s.minutesToBreakQuieter), tips::quieter });
    }

    // The hearing dose: how much of today's is used, how long it lasts at this level
    // (used up: the status line says so).
    const auto doseUsed = juce::String (juce::roundToInt (s.doseToday)) + " % USED";
    const auto doseLeft = playing && ! s.dayDone ? approx (s.minutesToFullDose) + " LEFT" : juce::String();
    const auto& doseTip = s.dayDone ? tips::dayDone : tips::doseToday;

    // The signal, honestly: its loudness, and against the moment you set your level.
    const auto signal = std::isfinite (s.lufsShortTerm) ? db (s.lufsShortTerm) + " LUFS" : juce::String ("SILENCE"); // not -inf LUFS
    const auto vsAnchor = ! (s.levelAnchored && playing) ? juce::String()
                        : std::abs (s.signalVsAnchorLu) < 1.0f ? levelName + " AS SET"
                                                               : levelName + " " + (s.signalVsAnchorLu > 0.0f ? "+" : "")
                                                                   + juce::String (juce::roundToInt (s.signalVsAnchorLu)) + " LU";

    // Why the battery drains faster, only the reasons that really matter (+10 % or more).
    const auto& f = s.factors;
    const auto dayLoad = 1.0 + model::dailyLoadFactorMax * std::min (s.doseToday / 100.0, 1.0);
    std::vector<Item> reasons;

    auto reason = [&] (const char* name, double factor, const juce::String& tip)
    {
        if (playing && factor >= 1.095)
            reasons.push_back ({ juce::String (name) + " +" + juce::String (juce::roundToInt ((factor - 1.0) * 100.0)) + " %", tip });
    };

    reason ("BRIGHT MIX", f.harshness, tips::harsh (s.presenceToBody, s.highsToBody));
    reason ("LITTLE DYNAMICS", f.dynamics, tips::dynamics (s.psr));
    reason ("SAME LOOP", f.repetition, tips::loop (s.loopBars, s.loopingSeconds, s.loopStrain));
    reason ("LATE HOUR", f.night, tips::lateHour);
    reason ("FULL DAY", dayLoad, tips::todaysDose);

    if (! reasons.empty())
        reasons.insert (reasons.begin(), { "DRAINS FASTER:", tips::drain });

    m.factors = reasons;

    // COMPACT: the dose, then the signal (and the strongest reason, if any).
    std::vector<Item> doseItems { { "HEARING DOSE " + doseUsed, doseTip } };
    if (doseLeft.isNotEmpty())
        doseItems.push_back ({ doseLeft, tips::doseLeft });

    std::vector<Item> signalItems { { signal, tips::signal } };
    if (vsAnchor.isNotEmpty())
        signalItems.push_back ({ vsAnchor, tips::vsAnchor });
    if (reasons.size() > 1)
        signalItems.push_back (reasons[1]);

    m.compactLines = { doseItems, signalItems };

    // POSTER: five numbers.
    m.stats = { { "HEARING DOSE" + (doseLeft.isNotEmpty() ? separator + doseLeft : juce::String()), doseUsed, doseTip },
                { "HEARING DOSE 7 DAYS", juce::String (juce::roundToInt (s.doseWeek)) + " % USED", tips::doseWeek },
                { "LISTENED TODAY", hoursMinutes (s.listeningSecondsToday), tips::listened },
                { "SINCE BREAK", hoursMinutes (s.secondsSinceBreak), tips::sinceBreak },
                { "SIGNAL" + (vsAnchor.isNotEmpty() ? separator + vsAnchor : juce::String()), signal, tips::signal } };

    return m;
}

juce::String tooltipAt (const MeterModel& m, MeterSize size, juce::Point<float> p, const Fonts& fonts)
{
    const auto L = layoutFor (size);
    const auto inner = L.width - 2.0f * L.margin;

    if (coachArea (L).contains (p))
        return m.coach.isNotEmpty() ? tips::coach : juce::String();

    {
        const auto bounds = itemBounds (m.timeLine, fonts.mono (L.timeSize, true), timeLine (L));

        for (size_t k = 0; k < bounds.size(); ++k)
            if (bounds[k].contains (p))
                return m.timeLine[k].tip;
    }

    if (size == MeterSize::compact)
    {
        for (size_t i = 0; i < m.compactLines.size(); ++i)
        {
            const auto bounds = itemBounds (m.compactLines[i], fonts.mono (L.dataSize), compactLine (L, i));

            for (size_t k = 0; k < bounds.size(); ++k)
                if (bounds[k].contains (p))
                    return m.compactLines[i][k].tip;
        }
    }
    else
    {
        for (size_t i = 0; i < std::min (statCount, m.stats.size()); ++i)
            if (statColumn (L, i).contains (p))
                return m.stats[i].tip;

        const auto bounds = itemBounds (m.factors, fonts.mono (11.0f), factorsLine (L));

        for (size_t k = 0; k < bounds.size(); ++k)
            if (bounds[k].contains (p))
                return m.factors[k].tip;
    }

    if (juce::Rectangle<float> (L.margin, L.statusTop, inner, L.statusSize * 1.5f).contains (p))
        return m.status.isNotEmpty() ? tips::status : juce::String();

    // The battery number sits right, below the word.
    const auto batteryFont = fonts.grotesk (L.batterySize);
    const auto batteryWidth = juce::GlyphArrangement::getStringWidth (batteryFont, m.battery);
    if (juce::Rectangle<float> (L.margin + inner - batteryWidth, L.batteryTop, batteryWidth, L.batterySize * groteskCapHeight).contains (p))
        return tips::battery;

    if (juce::Rectangle<float> (L.margin, L.wordTop, inner, L.wordMax * groteskCapHeight).contains (p))
        return tips::word (m.word);

    return {};
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
