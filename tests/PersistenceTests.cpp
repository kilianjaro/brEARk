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
            e.history = { { 20369, 55.0, 7200.0, 4 }, { 20370, 81.5, 10800.0, 2 } };

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
                expectEquals ((int) x.history.size(), 2);
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

        file.deleteFile();
    }
};

static PersistenceTests persistenceTests;

} // namespace breark
