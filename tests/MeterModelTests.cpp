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
            expect (unplugged.alarm && unplugged.status.contains ("3:10 OF 5:00"));

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

        beginTest ("POSTER stats: six labels, six values, each with a two-part explanation");
        {
            const auto m = wordFor ([] (Snapshot& s) { s.doseToday = 18.0f; s.listeningSecondsToday = 65.0 * 60.0; });
            expectEquals ((int) m.stats.size(), 6);
            expectEquals (m.stats[0].value, juce::String ("18 %"));
            expectEquals (m.stats[2].value, juce::String ("1:05"));

            for (const auto& stat : m.stats)
                expect (stat.tip.containsChar ('\n') && stat.tip.length() > 40, stat.label + " has no explanation");
        }

        beginTest ("Why it drains, in plain words (M9): percentages, not x-factors");
        {
            const auto m = wordFor ([] (Snapshot& s)
            {
                s.drainFactor = 1.21f;
                s.factors = { 1.03, 1.16, 1.0, 1.0 };
                s.doseToday = 18.0f;
            });

            expectEquals (m.factors[0].text, juce::String ("BATTERY DRAINS 21 % FASTER:"));
            expectEquals (m.factors[1].text, juce::String ("HARSH HIGHS +3 %"));
            expectEquals (m.factors[2].text, juce::String ("LITTLE DYNAMICS +16 %"));
            expectEquals (m.factors[3].text, juce::String ("SAME LOOP +0 %"));
            expectEquals (m.factors[5].text, juce::String ("TODAY'S DOSE +9 %"));

            for (const auto& item : m.factors)
                expect (item.tip.containsChar ('\n'), item.text + " has no explanation");

            for (const auto& line : m.compactLines)
                for (const auto& item : line)
                    expect (! item.text.contains (" x1") && item.tip.containsChar ('\n'), item.text);

            expect (wordFor ([] (Snapshot& s) { s.silent = true; }).factors[0].text.startsWith ("SILENCE"));
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
                s.monitorOffsetDb = -6.0;
            });
            expectEquals (playing.timeLine[0].text, juce::String ("BREAK IN ~25 MIN"));
            expectEquals (playing.timeLine[1].text, juce::String ("-6 dB: ~1:40 H"));
            expectEquals (playing.timeLine[2].text, juce::String ("DOSE FULL IN ~3:10 H"));
            expectEquals (playing.timeLine[3].text, juce::String ("VOLUME -6 dB"));

            const auto resting = wordFor ([] (Snapshot& s) { s.silent = true; s.battery = 40.0f; s.minutesToRecover = 8.4; });
            expect (resting.timeLine[0].text.startsWith ("RECOVERING") && resting.timeLine[0].text.endsWith ("~8 MIN"));

            for (const auto& item : playing.timeLine)
                expect (item.tip.containsChar ('\n'), item.text + " has no explanation");
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
