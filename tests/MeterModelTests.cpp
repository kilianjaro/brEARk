#include "ui/MeterView.h"
#include "ui/Tooltips.h"

#include <juce_core/juce_core.h>

namespace breark
{
class MeterModelTests final : public juce::UnitTest
{
public:
    MeterModelTests() : juce::UnitTest ("Meter view model (state words)", "brEARk") {}

    void runTest() override
    {
        auto wordFor = [] (std::function<void (Snapshot&)> setUp, bool active = true)
        {
            Snapshot s;
            s.silent = false;
            setUp (s);
            return ui::makeMeterModel (s, "line", active, PowerDown::Stage::run);
        };

        beginTest ("Battery decides FRESH. / OK. / TIRING., the mass turns red at 20 %");
        {
            expectEquals (wordFor ([] (Snapshot& s) { s.battery = 92.0f; }).word, juce::String ("FRESH."));
            expectEquals (wordFor ([] (Snapshot& s) { s.battery = 64.0f; }).word, juce::String ("OK."));
            expectEquals (wordFor ([] (Snapshot& s) { s.battery = 31.0f; }).word, juce::String ("TIRING."));

            const auto low = wordFor ([] (Snapshot& s) { s.battery = 14.0f; });
            expect (low.massRed && ! low.alarm);
            expectWithinAbsoluteError (low.batteryFraction, 0.14, 1.0e-6);
        }

        beginTest ("Break due: BREAK., then STOP. in signal red once it's ignored for 15 min");
        {
            const auto due = wordFor ([] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.secondsSinceDue = 60.0; });
            expectEquals (due.word, juce::String ("BREAK."));
            expect (! due.alarm);

            const auto ignored = wordFor ([] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.secondsSinceDue = 15.0 * 60.0; });
            expectEquals (ignored.word, juce::String ("STOP."));
            expect (ignored.alarm && ! ignored.massRed, "the whole window is red, not just the mass");
        }

        beginTest ("Breaks, unplugged, silence, inactive");
        {
            expectEquals (wordFor ([] (Snapshot& s) { s.breakPhase = BreakPhase::onBreak; s.breakSecondsLeft = 432.0; }).word, juce::String ("RESTING."));
            expect (wordFor ([] (Snapshot& s) { s.breakPhase = BreakPhase::onBreak; s.breakSecondsLeft = 432.0; }).status.contains ("7:12 LEFT"));

            const auto unplugged = wordFor ([] (Snapshot& s) { s.breakPhase = BreakPhase::unplugged; s.unpluggedSeconds = 190.0; });
            expectEquals (unplugged.word, juce::String ("UNPLUGGED."));
            expect (unplugged.alarm && unplugged.status.contains ("SOUND BACK IN 1:50"));

            expectEquals (wordFor ([] (Snapshot& s) { s.recovering = true; }).word, juce::String ("RESTING."));
            expectEquals (wordFor ([] (Snapshot&) {}, false).word, juce::String ("INACTIVE."));

            const auto off = wordFor ([] (Snapshot& s) { s.hostOff = true; s.breakPhase = BreakPhase::unplugged; });
            expectEquals (off.word, juce::String ("OFF."), "switched off in Live wins over the frozen break state");
            expect (! off.alarm && off.status.contains ("NOT COUNTING"));
        }

        beginTest ("Exports and test mode show in the status line, with a real middle dot");
        {
            const auto exporting = wordFor ([] (Snapshot& s) { s.rendering = true; });
            expectEquals (exporting.status, "EXPORT" + ui::separator + "NOT COUNTING");
            expect (exporting.status.contains (juce::String::fromUTF8 ("\xc2\xb7")), "not Latin-1 garbage");

            expect (wordFor ([] (Snapshot& s) { s.timeWarp = 60; }).status.startsWith ("TEST MODE x60"));
        }

        beginTest ("POSTER stats (M11): five, the hearing dose first, never dB at your ears");
        {
            const auto m = wordFor ([] (Snapshot& s) { s.doseToday = 18.0f; s.listeningSecondsToday = 65.0 * 60.0; });
            expectEquals ((int) m.stats.size(), 5);
            expectEquals (m.stats[0].value, juce::String ("18 % USED"));
            expect (m.stats[0].label.startsWith ("HEARING DOSE"));
            expectEquals (m.stats[2].value, juce::String ("1:05"));
            expect (m.stats[4].value.endsWith ("LUFS"));

            for (const auto& stat : m.stats)
            {
                expect (stat.tip.containsChar ('\n') && stat.tip.length() > 40, stat.label + " has no explanation");
                expect (! stat.value.contains (" dB"), stat.label + " shows dB");
            }
        }

        beginTest ("Why it drains (M11): only reasons that add 10 % or more, in plain words");
        {
            const auto m = wordFor ([] (Snapshot& s)
            {
                s.drainFactor = 1.21f;
                s.factors = { 1.03, 1.16, 1.0, 1.0 };
                s.doseToday = 18.0f;
            });

            expectEquals ((int) m.factors.size(), 2, "the head and one reason: bright (+3 %) and a full day (+9 %) stay hidden");
            expectEquals (m.factors[0].text, juce::String ("DRAINS FASTER:"));
            expectEquals (m.factors[1].text, juce::String ("LITTLE DYNAMICS +16 %"));

            for (const auto& item : m.factors)
                expect (item.tip.containsChar ('\n'), item.text + " has no explanation");

            for (const auto& line : m.compactLines)
                for (const auto& item : line)
                    expect (! item.text.contains (" x1") && item.tip.containsChar ('\n'), item.text);

            expect (wordFor ([] (Snapshot& s) { s.factors = { 1.03, 1.05, 1.0, 1.0 }; }).factors.empty(), "nothing that matters: no line");
            expect (wordFor ([] (Snapshot& s) { s.silent = true; s.factors = { 1.3, 1.2, 1.0, 1.0 }; }).factors.empty(), "silence drains nothing");
        }

        beginTest ("End of day and time left (M10)");
        {
            const auto done = wordFor ([] (Snapshot& s) { s.dayDone = true; s.battery = 60.0f; });
            expectEquals (done.word, juce::String ("DONE."));
            expect (! done.alarm && done.status.contains ("CALL IT A DAY"));

            const auto unplugged = wordFor ([] (Snapshot& s) { s.dayDone = true; s.unpluggedForDay = true; s.breakPhase = BreakPhase::unplugged; });
            expectEquals (unplugged.word, juce::String ("DONE."));
            expect (unplugged.alarm && unplugged.status.contains ("05:00"));

            const auto playing = wordFor ([] (Snapshot& s)
            {
                s.battery = 60.0f;
                s.minutesToBreak = 25.2;
                s.minutesToBreakQuieter = 100.0;
                s.minutesToFullDose = 190.0;
                s.levelAnchored = true;
                s.signalVsAnchorLu = 4.4f;
            });
            expectEquals (playing.timeLine[0].text, juce::String ("BREAK IN ~25 MIN"));
            expectEquals (playing.timeLine[1].text, juce::String ("AT QUIET ~1:40 H"));
            expectEquals (playing.compactLines[0][0].text, juce::String ("HEARING DOSE 0 % USED"));
            expectEquals (playing.compactLines[0][1].text, juce::String ("~3:10 H LEFT"));
            expectEquals (playing.compactLines[1][1].text, juce::String ("NORMAL +4 LU"));
            expect (! playing.compactLines[1][0].text.contains (" dB"), "no dB at the ears: only the signal in LUFS");

            const auto resting = wordFor ([] (Snapshot& s) { s.silent = true; s.battery = 40.0f; s.minutesToRecover = 8.4; });
            expectEquals (resting.timeLine[0].text, juce::String ("90 % IN ~8 MIN"));

            const auto due = wordFor ([] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.suggestedBreakMinutes = 12.4f; });
            expectEquals (due.timeLine[0].text, juce::String ("TAKE ~12 MIN OF QUIET"));

            for (const auto& item : playing.timeLine)
                expect (item.tip.containsChar ('\n'), item.text + " has no explanation");
        }

        beginTest ("M11.2: one line, one job: no state says the same thing twice");
        {
            const std::function<void (Snapshot&)> states[] = {
                [] (Snapshot& s) { s.dayDone = true; s.doseToday = 413.0f; s.battery = 60.0f; s.minutesToBreak = 25.0; s.minutesToFullDose = 0.0; },
                [] (Snapshot& s) { s.dayDone = true; s.doseToday = 413.0f; s.silent = true; s.battery = 60.0f; s.minutesToRecover = 6.0; },
                [] (Snapshot& s) { s.dayDone = true; s.unpluggedForDay = true; s.breakPhase = BreakPhase::unplugged; s.silent = true; },
                [] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.secondsSinceDue = 60.0; },
                [] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.secondsSinceDue = 1000.0; },
                [] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.silent = true; s.minutesToRecover = 12.0; },
                [] (Snapshot& s) { s.breakPhase = BreakPhase::unplugged; s.unpluggedSeconds = 100.0; s.silent = true; s.minutesToRecover = 9.0; },
                [] (Snapshot& s) { s.breakPhase = BreakPhase::onBreak; s.breakSecondsLeft = 300.0; s.silent = true; s.battery = 50.0f; s.minutesToRecover = 9.0; },
                [] (Snapshot& s) { s.recovering = true; s.silent = true; s.battery = 50.0f; s.minutesToRecover = 9.0; },
            };

            for (const auto& state : states)
            {
                const auto m = wordFor (state);
                juce::StringArray texts { m.word, m.status };

                for (const auto& item : m.timeLine)
                    texts.add (item.text);

                for (const auto& stat : m.stats)
                    texts.add (stat.label + " " + stat.value);

                const auto all = texts.joinIntoString (" | ");

                for (const auto* phrase : { "USED UP", "CALL IT A DAY", "BREAK DUE", "RECOVERING", "UNPLUGGED", "EMPTY", "05:00" })
                    expect (all.indexOf (phrase) == all.lastIndexOf (phrase), "POSTER says '" + juce::String (phrase) + "' twice: " + all);

                juce::StringArray compact { m.word, m.status };

                for (const auto& item : m.timeLine)
                    compact.add (item.text);

                for (const auto& line : m.compactLines)
                    for (const auto& item : line)
                        compact.add (item.text);

                const auto small = compact.joinIntoString (" | ");

                for (const auto* phrase : { "USED UP", "CALL IT A DAY", "BREAK DUE", "RECOVERING", "UNPLUGGED", "EMPTY", "05:00" })
                    expect (small.indexOf (phrase) == small.lastIndexOf (phrase), "COMPACT says '" + juce::String (phrase) + "' twice: " + small);
            }
        }

        beginTest ("Hover: the explanation under the mouse");
        {
            const ui::Fonts fonts;
            const auto m = wordFor ([] (Snapshot& s) { s.battery = 64.0f; });

            // POSTER: the first stat column, the battery number, the word, empty space.
            expectEquals (ui::tooltipAt (m, ui::MeterSize::poster, { 60.0f, 420.0f }, fonts), m.stats[0].tip);
            expectEquals (ui::tooltipAt (m, ui::MeterSize::poster, { 900.0f, 330.0f }, fonts), ui::tips::battery);
            expect (ui::tooltipAt (m, ui::MeterSize::poster, { 200.0f, 150.0f }, fonts).startsWith ("Your ear battery in one word"));
            expect (ui::tooltipAt (m, ui::MeterSize::poster, { 600.0f, 40.0f }, fonts).isEmpty());

            // COMPACT: the time line first, then the first item of the data lines (DOSE).
            expectEquals (ui::tooltipAt (m, ui::MeterSize::compact, { 30.0f, 236.0f }, fonts), ui::tips::timeLeft);
            expectEquals (ui::tooltipAt (m, ui::MeterSize::compact, { 30.0f, 252.0f }, fonts), ui::tips::doseToday);
        }
    }
};

static MeterModelTests meterModelTests;

} // namespace breark
