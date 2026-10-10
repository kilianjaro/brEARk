#include "engine/BreakController.h"
#include "engine/ExposureModel.h"

#include <juce_core/juce_core.h>

namespace breark
{
class BreakControllerTests final : public juce::UnitTest
{
public:
    BreakControllerTests() : juce::UnitTest ("Break controller (escalation ladder)", "brEARk") {}

    void runTest() override
    {
        beginTest ("ESCALATING: heads-up at 20 %, sound at 0 %, then +10 firm, +15 and +20 brutal");
        {
            BreakController breaks;
            auto alerts = drain (breaks, Strictness::escalating);

            expectEquals (count (alerts, AlertKind::headsUp), 1);
            expectEquals (count (alerts, AlertKind::breakDue), 1);
            expect (! find (alerts, AlertKind::headsUp).withSound, "the heads-up must be silent");
            expect (find (alerts, AlertKind::breakDue).withSound);

            const auto reminders = listen (breaks, Strictness::escalating, 0.0, 20.0 * 60.0 + 1.0);
            expectEquals (count (reminders, AlertKind::reminder), 3); // +10, +15, +20 min
            expect (reminders[0].tone == Tone::firm);
            expect (reminders[1].tone == Tone::brutal && reminders[2].tone == Tone::brutal);
            expect (breaks.getPhase() == BreakPhase::due);
        }

        beginTest ("GENTLE: one sound at 0 %, nothing before, nothing after");
        {
            BreakController breaks;
            auto alerts = drain (breaks, Strictness::gentle);
            expectEquals ((int) alerts.size(), 1);
            expect (alerts[0].kind == AlertKind::breakDue && alerts[0].withSound);
            expect (listen (breaks, Strictness::gentle, 0.0, 60.0 * 60.0).empty());
        }

        beginTest ("HARD: unplugs 10 min after empty, with sound");
        {
            BreakController breaks;
            drain (breaks, Strictness::hard);

            auto alerts = listen (breaks, Strictness::hard, 0.0, 9.0 * 60.0);
            expect (alerts.empty() && ! breaks.isUnplugRequested());

            alerts = listen (breaks, Strictness::hard, 0.0, 61.0);
            expectEquals (count (alerts, AlertKind::unplug), 1);
            expect (breaks.isUnplugRequested());
        }

        beginTest ("HARD while recording: announces once, unplugs when the take is done");
        {
            BreakController breaks;
            drain (breaks, Strictness::hard);

            auto alerts = listen (breaks, Strictness::hard, 0.0, 15.0 * 60.0, true);
            expectEquals (count (alerts, AlertKind::unplugAfterTake), 1);
            expectEquals (count (alerts, AlertKind::unplug), 0);
            expect (! breaks.isUnplugRequested(), "never cut a take");

            alerts = listen (breaks, Strictness::hard, 0.0, 1.0, false);
            expectEquals (count (alerts, AlertKind::unplug), 1);
        }

        beginTest ("HARD, unplugged: stays off for the 5 min minimum break, then plugs back in");
        {
            BreakController breaks;
            drain (breaks, Strictness::hard);
            listen (breaks, Strictness::hard, 0.0, 10.0 * 60.0 + 1.0);
            expect (breaks.isUnplugRequested());

            // Pressing BREAK changes nothing: this already is a (forced) break.
            breaks.toggleBreak (12.0);
            // (The unplug fired ~2 s before the 10 min of listening above ended.)
            auto alerts = listen (breaks, Strictness::hard, 10.0, 4.0 * 60.0 + 50.0, false, true);
            expect (alerts.empty() && breaks.isUnplugRequested(), "the mute lifted too early");

            alerts = listen (breaks, Strictness::hard, 55.0, 10.0, false, true);
            expectEquals (count (alerts, AlertKind::replugged), 1);
            expect (! breaks.isUnplugRequested());
            expect (breaks.getPhase() == BreakPhase::listening, "recovered past 30 %: start over");
        }

        beginTest ("HARD, unplugged, still drained after the minimum break: the ladder runs again");
        {
            BreakController breaks;
            drain (breaks, Strictness::hard);
            listen (breaks, Strictness::hard, 0.0, 10.0 * 60.0 + 1.0);

            auto alerts = listen (breaks, Strictness::hard, 5.0, 5.0 * 60.0 + 1.0);
            expectEquals (count (alerts, AlertKind::replugged), 1);
            expect (breaks.getPhase() == BreakPhase::due);

            alerts = listen (breaks, Strictness::hard, 5.0, 10.0 * 60.0 + 1.0);
            expectEquals (count (alerts, AlertKind::unplug), 1);
        }

        beginTest ("SNOOZE buys exactly 10 minutes, then the plug comes out again");
        {
            BreakController breaks;
            drain (breaks, Strictness::hard);
            listen (breaks, Strictness::hard, 0.0, 10.0 * 60.0 + 1.0);

            breaks.snooze();
            auto alerts = breaks.advance (inputs (Strictness::hard, 0.0, false));
            expectEquals (count (alerts, AlertKind::snoozed), 1);
            expect (! breaks.isUnplugRequested());

            alerts = listen (breaks, Strictness::hard, 0.0, 10.0 * 60.0 - 2.0);
            expect (alerts.empty() && ! breaks.isUnplugRequested(), "unplugged before the snooze ran out");

            alerts = listen (breaks, Strictness::hard, 0.0, 3.0);
            expectEquals (count (alerts, AlertKind::unplug), 1);
        }

        beginTest ("Leaving HARD mode plugs back in at once (the escape hatch)");
        {
            BreakController breaks;
            drain (breaks, Strictness::hard);
            listen (breaks, Strictness::hard, 0.0, 10.0 * 60.0 + 1.0);
            expect (breaks.isUnplugRequested());

            const auto alerts = breaks.advance (inputs (Strictness::escalating, 0.0, false));
            expectEquals (count (alerts, AlertKind::replugged), 1);
            expect (! breaks.isUnplugRequested());
        }

        beginTest ("Silence pauses the escalation clock");
        {
            BreakController breaks;
            drain (breaks, Strictness::escalating);
            const auto before = breaks.getSecondsSinceDue();

            for (int s = 0; s < 30 * 60; ++s)
                expect (breaks.advance (inputs (Strictness::escalating, 0.0, true)).empty());

            expectEquals (breaks.getSecondsSinceDue(), before);
        }

        beginTest ("Recovered to 30 %: everything starts over");
        {
            BreakController breaks;
            drain (breaks, Strictness::escalating);
            breaks.advance (inputs (Strictness::escalating, 35.0, true));
            expect (breaks.getPhase() == BreakPhase::listening);

            const auto alerts = drain (breaks, Strictness::escalating);
            expectEquals (count (alerts, AlertKind::headsUp), 1);
            expectEquals (count (alerts, AlertKind::breakDue), 1);
        }

        beginTest ("The BREAK button: a countdown without nagging, then 'welcome back'; pressing again ends it");
        {
            BreakController breaks;
            drain (breaks, Strictness::escalating);

            breaks.toggleBreak (12.0);
            auto alerts = breaks.advance (inputs (Strictness::escalating, 0.0, false));
            expectEquals (count (alerts, AlertKind::breakStarted), 1);
            expect (breaks.getPhase() == BreakPhase::onBreak);
            expectWithinAbsoluteError (breaks.getBreakSecondsLeft(), 12.0 * 60.0, 1.0e-9);

            // Even with audio playing, a break never nags.
            alerts = listen (breaks, Strictness::escalating, 0.0, 11.0 * 60.0);
            expect (alerts.empty());

            alerts = listen (breaks, Strictness::escalating, 95.0, 61.0, false, true);
            expectEquals (count (alerts, AlertKind::breakOver), 1);
            expect (breaks.getPhase() == BreakPhase::listening);

            breaks.toggleBreak (5.0);
            breaks.advance (inputs (Strictness::escalating, 95.0, true));
            breaks.toggleBreak (5.0);
            breaks.advance (inputs (Strictness::escalating, 95.0, true));
            expect (breaks.getPhase() == BreakPhase::listening, "pressing BREAK again ends the break");
        }

        beginTest ("On a BREAK, recovery starts at once (no 60 s onset)");
        {
            ExposureState state;
            state.battery = 0.0;
            ExposureModel model (state);

            ListeningPeriod period;
            period.seconds = 300.0;
            period.onBreak = true;
            model.advance (period, juce::Time (2026, 9, 10, 12, 0));
            expectWithinAbsoluteError (model.getState().battery, 100.0 * (1.0 - std::exp (-1.0)), 0.1);
        }
    }

private:
    static BreakInputs inputs (Strictness strictness, double battery, bool silent, bool recording = false)
    {
        BreakInputs in;
        in.seconds = 1.0;
        in.battery = battery;
        in.silent = silent;
        in.recording = recording;
        in.suggestedBreakMinutes = 12.0;
        in.strictness = strictness;
        return in;
    }

    // Battery from 40 % to empty while listening.
    static std::vector<Alert> drain (BreakController& breaks, Strictness strictness)
    {
        std::vector<Alert> all;

        for (double battery = 40.0; battery >= 0.0; battery -= 0.5)
            for (const auto& a : breaks.advance (inputs (strictness, battery, false)))
                all.push_back (a);

        return all;
    }

    static std::vector<Alert> listen (BreakController& breaks, Strictness strictness, double battery, double seconds,
                                      bool recording = false, bool silent = false)
    {
        std::vector<Alert> all;

        for (double t = 0.0; t < seconds; t += 1.0)
            for (const auto& a : breaks.advance (inputs (strictness, battery, silent, recording)))
                all.push_back (a);

        return all;
    }

    static int count (const std::vector<Alert>& alerts, AlertKind kind)
    {
        return (int) std::count_if (alerts.begin(), alerts.end(), [kind] (const Alert& a) { return a.kind == kind; });
    }

    static Alert find (const std::vector<Alert>& alerts, AlertKind kind)
    {
        const auto it = std::find_if (alerts.begin(), alerts.end(), [kind] (const Alert& a) { return a.kind == kind; });
        return it != alerts.end() ? *it : Alert {};
    }
};

static BreakControllerTests breakControllerTests;

} // namespace breark
