#include "ui/MeterView.h"

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

        beginTest ("POSTER stats: six labels, six values");
        {
            const auto m = wordFor ([] (Snapshot& s) { s.doseToday = 18.0f; s.listeningSecondsToday = 65.0 * 60.0; });
            expectEquals (m.statLabels.size(), 6);
            expectEquals (m.statValues.size(), 6);
            expectEquals (m.statValues[0], juce::String ("18 %"));
            expectEquals (m.statValues[2], juce::String ("1:05"));
        }
    }
};

static MeterModelTests meterModelTests;

} // namespace breark
