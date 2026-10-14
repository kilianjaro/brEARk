#include "engine/Persistence.h"

#include <juce_core/juce_core.h>

namespace breark
{
class PersistenceTests final : public juce::UnitTest
{
public:
    PersistenceTests() : juce::UnitTest ("Persistence", "brEARk") {}

    void runTest() override
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("brEARk-persistence-test", ".json");

        beginTest ("Everything survives a save/load round trip");
        {
            PersistedState original;
            original.settings = { Setup::headphones, LevelPreset::loud };
            original.savedAtMs = 1760090000000;

            auto& e = original.exposure;
            e.day = 20371;
            e.battery = 42.5;
            e.doseToday = 17.25;
            e.listeningSecondsToday = 5400.0;
            e.silenceSeconds = 12.0;
            e.secondsSinceBreak = 1800.0;
            e.breaksToday = 3;
            e.referenceLufs = -9.75;
            e.referenceLevelA = -16.5;
            e.referencePlaybackSeconds = 4321.0;
            e.offSecondsToday = 900.0;
            e.longestStretchToday = 4321.0;
            e.emptyBatteryToday = 2;
            e.levelAnchored = true;
            e.lastSummaryWeek = 2924;
            e.history = { { 20369, 55.0, 7200.0, 4 }, { 20370, 81.5, 10800.0, 2, 0, 0, 1500.0, 3600.0, 3 } };
            e.todayCurve = { { 270, 100 }, { 271, 99 }, { 300, 60, true }, { 400, 31 } };

            expect (persistence::save (file, original));

            const auto loaded = persistence::load (file);
            expect (loaded.has_value());

            if (loaded)
            {
                const auto& x = loaded->exposure;
                expect (loaded->settings.setup == Setup::headphones);
                expect (loaded->settings.preset == LevelPreset::loud);
                expectEquals (loaded->savedAtMs, original.savedAtMs);
                expectEquals (x.day, e.day);
                expectEquals (x.battery, e.battery);
                expectEquals (x.doseToday, e.doseToday);
                expectEquals (x.listeningSecondsToday, e.listeningSecondsToday);
                expectEquals (x.silenceSeconds, e.silenceSeconds);
                expectEquals (x.secondsSinceBreak, e.secondsSinceBreak);
                expectEquals (x.breaksToday, e.breaksToday);
                expectEquals (x.referenceLufs, e.referenceLufs);
                expectEquals (x.referencePlaybackSeconds, e.referencePlaybackSeconds);
                expectEquals (x.referenceLevelA, -16.5);
                expectEquals ((int) x.history.size(), 2);
                expectEquals ((int) x.todayCurve.size(), 4);
                expect (x.todayCurve[2].off && ! x.todayCurve[3].off && ! x.todayCurve[0].off);
                expectEquals (x.todayCurve[3].minute, 400);
                expectEquals (x.todayCurve[3].battery, 31);
                expectEquals (x.offSecondsToday, 900.0);
                expectEquals (x.history[1].offSeconds, 1500.0);
                expectEquals (x.history[0].offSeconds, 0.0);
                expectEquals (x.longestStretchToday, 4321.0);
                expectEquals (x.emptyBatteryToday, 2);
                expect (x.levelAnchored);
                expectEquals (x.lastSummaryWeek, 2924);
                expectEquals (x.history[1].longestStretchSeconds, 3600.0);
                expectEquals (x.history[1].emptyBattery, 3);
                expectEquals (x.history[1].day, 20370);
                expectEquals (x.history[1].dose, 81.5);
                expectEquals (x.history[1].breaks, 2);
            }
        }

        beginTest ("Missing, corrupt or foreign files fall back to defaults");
        {
            file.deleteFile();
            expect (! persistence::load (file).has_value());

            file.replaceWithText ("{ this is not json");
            expect (! persistence::load (file).has_value());

            file.replaceWithText (R"({ "version": 999, "settings": {}, "exposure": {} })");
            expect (! persistence::load (file).has_value());
        }

        beginTest ("Files from before M8 (no OFF fields) still load");
        {
            file.replaceWithText (R"({ "version": 1, "savedAtMs": 1, "settings": { "setup": "monitors" },
                                       "exposure": { "day": 20370, "battery": 55.0, "todayCurve": [ 270, 100, 271, 99 ],
                                                     "history": [ { "day": 20369, "dose": 12.0 } ] } })");
            const auto loaded = persistence::load (file);
            expect (loaded.has_value());

            if (loaded)
            {
                expectEquals (loaded->exposure.offSecondsToday, 0.0);
                expectEquals (loaded->exposure.history[0].offSeconds, 0.0);
                expect (! loaded->exposure.todayCurve[1].off);
                expectEquals (loaded->exposure.todayCurve[1].battery, 99);
                expect (std::isnan (loaded->exposure.referenceLevelA), "before M11.2: the A reference is learned on the next play");
            }
        }

        file.deleteFile();
    }
};

static PersistenceTests persistenceTests;

} // namespace breark
