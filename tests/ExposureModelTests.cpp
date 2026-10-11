#include "engine/ExposureModel.h"

#include <juce_core/juce_core.h>

namespace breark
{
class ExposureModelTests final : public juce::UnitTest
{
public:
    ExposureModelTests() : juce::UnitTest ("Exposure model", "brEARk") {}

    void runTest() override
    {
        const auto noon = juce::Time (2026, 9, 10, 12, 0); // 10 Oct 2026, local time

        // With the reference equal to the playback loudness, the perceived level
        // equals the preset level: 70 / 80 / 90 dB, +3 dB on headphones.
        beginTest ("Battery drain times match the plan (IMPLEMENTATION.md §4.3, 5 dB rule)");
        {
            struct Case { LevelPreset preset; Setup setup; double expectedMinutes; };

            for (const auto& c : { Case { LevelPreset::quiet,  Setup::monitors,   149.3 },
                                   Case { LevelPreset::normal, Setup::monitors,    70.0 },
                                   Case { LevelPreset::normal, Setup::headphones,  51.3 },
                                   Case { LevelPreset::loud,   Setup::monitors,    22.4 } })
            {
                auto model = modelAt (-14.0, { c.setup, c.preset });
                expectWithinAbsoluteError (minutesToEmpty (model, -14.0f, noon), c.expectedMinutes, 0.6);
            }
        }

        beginTest ("Recovery starts after 60 s of silence: 5 min → 63 %, 15 min → 95 %");
        {
            auto model = modelAt (-14.0);
            drainToEmpty (model, noon);

            model.advance (silence (60.0), noon);
            expectWithinAbsoluteError (model.getState().battery, 0.0, 0.01);
            expect (model.isRecovering());

            model.advance (silence (300.0), noon);
            expectWithinAbsoluteError (model.getState().battery, 63.2, 0.2);

            model.advance (silence (600.0), noon);
            expectWithinAbsoluteError (model.getState().battery, 95.0, 0.2);
        }

        beginTest ("Editing pauses under 60 s never recover the battery");
        {
            auto model = modelAt (-14.0);
            playFor (model, -14.0f, 35.0 * 60.0, noon);
            const auto before = model.getState().battery;

            for (int i = 0; i < 20; ++i)
            {
                model.advance (silence (30.0), noon);
                expect (model.getState().battery <= before + 1.0e-9);
                playFor (model, -14.0f, 30.0, noon);
            }
        }

        beginTest ("Dose: 8 h at 80 dB(A) = 100 %, 1 h at 89 dB(A) ≈ 100 % (3 dB rule)");
        {
            auto model = modelAt (-14.0);
            playFor (model, -14.0f, 8.0 * 3600.0, noon, -14.0f); // dose level = preset level = 80 dB(A)
            expectWithinAbsoluteError (model.getState().doseToday, 100.0, 0.1);

            auto louder = modelAt (-14.0);
            playFor (louder, -14.0f, 3600.0, noon, -5.0f); // +9 dB → 89 dB(A)
            expectWithinAbsoluteError (louder.getState().doseToday, 99.3, 0.2);
        }

        beginTest ("Bass-heavy material: same perceived level, less A-weighted dose");
        {
            auto model = modelAt (-9.0);
            playFor (model, -9.0f, 3600.0, noon, -22.0f); // your master: 13 dB(A) below its LUFS
            expectWithinAbsoluteError (model.getState().doseToday, 100.0 / 8.0 * std::pow (10.0, -1.3), 0.05);
        }

        beginTest ("Silence adds no dose");
        {
            auto model = modelAt (-14.0);
            model.advance (silence (3600.0), noon);
            expectEquals (model.getState().doseToday, 0.0);
        }

        beginTest ("A fresh reference learns your loudness within minutes");
        {
            auto model = modelAt (-14.0);
            playFor (model, -8.0f, 10.0 * 60.0, noon); // a running average: 6 dB off → 0.22 dB after 10 min
            expectWithinAbsoluteError (model.getState().referenceLufs, -8.0, 0.3);
        }

        beginTest ("An experienced reference follows playback loudness over ~2 h (hybrid)");
        {
            ExposureState state;
            state.referenceLufs = -14.0;
            state.referencePlaybackSeconds = 10.0 * 3600.0;
            ExposureModel model (state);

            playFor (model, -8.0f, 2.0 * 3600.0, noon);
            expectWithinAbsoluteError (model.getState().referenceLufs, -8.0 - 6.0 * std::exp (-1.0), 0.1);
        }

        beginTest ("The combined drain factor is capped");
        {
            auto model = modelAt (-14.0);
            expectWithinAbsoluteError (model.combinedFactor (1.2), 1.2, 1.0e-9);
            expectWithinAbsoluteError (model.combinedFactor (3.0), model::maxCombinedFactor, 1.0e-9);
        }

        beginTest ("The day rolls over at 05:00 and keeps a history");
        {
            auto model = modelAt (-14.0);
            const auto beforeFive = juce::Time (2026, 9, 11, 4, 59);
            const auto afterFive = juce::Time (2026, 9, 11, 5, 1);

            expectEquals (ExposureModel::dayNumber (noon), ExposureModel::dayNumber (beforeFive));
            expectEquals (ExposureModel::dayNumber (afterFive), ExposureModel::dayNumber (noon) + 1);

            playFor (model, -14.0f, 3600.0, beforeFive, -14.0f);
            const auto dose = model.getState().doseToday;
            model.advance (silence (120.0), afterFive);

            expectEquals (model.getState().doseToday, 0.0);
            expectEquals ((int) model.getState().history.size(), 1);
            expectWithinAbsoluteError (model.getState().history.back().dose, dose, 1.0e-9);
        }

        beginTest ("Today's curve: one battery point per minute, cleared when the day rolls over");
        {
            expectEquals (ExposureModel::minuteOfDay (juce::Time (2026, 9, 10, 5, 0)), 0);
            expectEquals (ExposureModel::minuteOfDay (juce::Time (2026, 9, 11, 4, 59)), 1439);

            auto model = modelAt (-14.0);

            for (int minute = 0; minute < 3; ++minute)
                for (int second = 0; second < 60; ++second)
                    playFor (model, -14.0f, 1.0, juce::Time (2026, 9, 10, 12, minute, second));

            const auto& curve = model.getState().todayCurve;
            expectEquals ((int) curve.size(), 3);
            expectEquals (curve[0].minute, 7 * 60);
            expectEquals (curve[2].battery, juce::roundToInt (model.getState().battery));

            model.advance (silence (60.0), juce::Time (2026, 9, 11, 5, 1));
            expectEquals ((int) model.getState().todayCurve.size(), 1, "a new day starts with a fresh curve");
        }

        beginTest ("Switched off in Live: nothing drains or recovers, the time is counted and marked OFF");
        {
            auto model = modelAt (-14.0);
            playFor (model, -14.0f, 600.0, noon, -14.0f);
            const auto before = model.getState();

            model.addOffTime (1800.0, juce::Time (2026, 9, 10, 12, 11));
            const auto& after = model.getState();

            expectEquals (after.battery, before.battery);
            expectEquals (after.doseToday, before.doseToday);
            expectEquals (after.listeningSecondsToday, before.listeningSecondsToday);
            expectEquals (after.silenceSeconds, before.silenceSeconds);
            expectEquals (after.secondsSinceBreak, before.secondsSinceBreak);
            expectEquals (after.offSecondsToday, 1800.0);
            expect (after.todayCurve.back().off && after.todayCurve.back().minute == 7 * 60 + 11);

            playFor (model, -14.0f, 1.0, juce::Time (2026, 9, 10, 12, 11, 30));
            expect (! model.getState().todayCurve.back().off, "within a minute the latest state wins");

            model.advance (silence (60.0), juce::Time (2026, 9, 11, 5, 1));
            expectEquals (model.getState().history.back().offSeconds, 1800.0);
            expectEquals (model.getState().offSecondsToday, 0.0, "a new day starts at 0");
        }

        beginTest ("Weekly dose = last 7 days / 5");
        {
            ExposureState state;
            state.day = ExposureModel::dayNumber (noon);
            state.doseToday = 100.0;

            for (int daysAgo : { 1, 2, 3, 4, 7 }) // the 7-days-ago entry is out of the window
                state.history.push_back ({ state.day - daysAgo, 100.0, 0.0, 0 });

            ExposureModel model (state);
            expectWithinAbsoluteError (model.weeklyDose(), 100.0, 1.0e-9);
        }

        beginTest ("Time away from Live counts as recovery");
        {
            auto model = modelAt (-14.0);
            drainToEmpty (model, noon);
            model.advance (silence (15.0 * 60.0), noon); // one long period
            expectWithinAbsoluteError (model.getState().battery, 100.0 - 100.0 * std::exp (-14.0 / 5.0), 0.1);
        }

        beginTest ("A silence of 5 min or more counts as a break");
        {
            auto model = modelAt (-14.0);
            playFor (model, -14.0f, 600.0, noon);
            model.advance (silence (240.0), noon);
            playFor (model, -14.0f, 1.0, noon);
            expectEquals (model.getState().breaksToday, 0);

            model.advance (silence (360.0), noon);
            playFor (model, -14.0f, 1.0, noon);
            expectEquals (model.getState().breaksToday, 1);
            expectWithinAbsoluteError (model.getState().secondsSinceBreak, 1.0, 1.0e-9);
        }

        beginTest ("Suggested break: ~12.5 min when empty, ×1.5 on a heavy day, 5 min minimum");
        {
            ExposureState state;
            state.battery = 0.0;
            expectWithinAbsoluteError (ExposureModel (state).suggestedBreakMinutes(), 1.0 + 5.0 * std::log (10.0), 0.01);

            state.doseToday = 100.0;
            expectWithinAbsoluteError (ExposureModel (state).suggestedBreakMinutes(), (1.0 + 5.0 * std::log (10.0)) * 1.5, 0.01);

            state.battery = 95.0;
            expectWithinAbsoluteError (ExposureModel (state).suggestedBreakMinutes(), 5.0, 1.0e-9);
        }
    }

private:
    static ExposureModel modelAt (double referenceLufs, Settings settings = {})
    {
        ExposureState state;
        state.referenceLufs = referenceLufs;
        ExposureModel model (state);
        model.setSettings (settings);
        return model;
    }

    static ListeningPeriod silence (double seconds)
    {
        ListeningPeriod period;
        period.seconds = seconds;
        return period;
    }

    static void playFor (ExposureModel& model, float lufs, double seconds, juce::Time now,
                         float levelA = -std::numeric_limits<float>::infinity())
    {
        for (double t = 0.0; t < seconds; t += 1.0)
        {
            ListeningPeriod period;
            period.seconds = std::min (1.0, seconds - t);
            period.lufsShortTerm = lufs;
            period.levelA = levelA;
            period.silent = false;
            model.advance (period, now);
        }
    }

    static double minutesToEmpty (ExposureModel& model, float lufs, juce::Time now)
    {
        double seconds = 0.0;

        while (model.getState().battery > 0.0 && seconds < 24.0 * 3600.0)
        {
            playFor (model, lufs, 1.0, now);
            seconds += 1.0;
        }

        return seconds / 60.0;
    }

    static void drainToEmpty (ExposureModel& model, juce::Time now)
    {
        minutesToEmpty (model, (float) model.getState().referenceLufs, now);
    }
};

static ExposureModelTests exposureModelTests;

} // namespace breark
