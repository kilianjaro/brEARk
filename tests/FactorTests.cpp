#include "engine/FatigueFactors.h"
#include "engine/LevelCreepTracker.h"
#include "engine/PlayheadTracker.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace breark
{
//==============================================================================
class FactorTests final : public juce::UnitTest
{
public:
    FactorTests() : juce::UnitTest ("Fatigue factors", "brEARk") {}

    void runTest() override
    {
        beginTest ("Factor mapping: neutral inputs change nothing, extremes hit the maxima");
        {
            FactorInputs neutral;
            neutral.presenceToBodyDb = -20.0;
            neutral.highsToBodyDb = -30.0;
            neutral.psrDb = 16.0;
            expectWithinAbsoluteError (computeFatigueFactors (neutral).product(), 1.0, 1.0e-9);

            FactorInputs extreme;
            extreme.presenceToBodyDb = -3.0;
            extreme.psrDb = 5.0;
            extreme.barPlays = 100.0;
            extreme.localHour = 2.0;
            const auto f = computeFatigueFactors (extreme);
            expectWithinAbsoluteError (f.harshness, 1.3, 1.0e-9);
            expectWithinAbsoluteError (f.dynamics, 1.2, 1.0e-9);
            expectWithinAbsoluteError (f.repetition, 1.5, 1.0e-9);
            expectWithinAbsoluteError (f.night, 1.25, 1.0e-9);
        }

        beginTest ("Factor mapping: PSR 8.4 dB (a limited master) → x1.16");
        {
            FactorInputs in;
            in.psrDb = 8.4;
            expectWithinAbsoluteError (computeFatigueFactors (in).dynamics, 1.16, 1.0e-9);
        }

        beginTest ("Night ramps from 22:00, is full 01:00–05:00, off by day");
        {
            auto nightAt = [] (double hour) { FactorInputs in; in.localHour = hour; return computeFatigueFactors (in).night; };
            expectWithinAbsoluteError (nightAt (21.5), 1.0, 1.0e-9);
            expectWithinAbsoluteError (nightAt (23.5), 1.125, 1.0e-9);
            expectWithinAbsoluteError (nightAt (3.0), 1.25, 1.0e-9);
            expectWithinAbsoluteError (nightAt (5.5), 1.0, 1.0e-9);
            expectWithinAbsoluteError (nightAt (14.0), 1.0, 1.0e-9);
        }

        beginTest ("Playhead: an 8-bar loop brace for 10 min counts ~40 plays per bar");
        {
            PlayheadTracker tracker;
            const auto quartersPerSecond = 140.0 / 60.0;
            double ppq = 128.0; // bar 32

            for (double t = 0.0; t < 600.0; t += 0.2)
            {
                tracker.update ({ true, ppq, 4.0 }, t);
                ppq += 0.2 * quartersPerSecond;

                if (ppq >= 160.0) // end of bar 39 → back to bar 32
                    ppq -= 32.0;
            }

            const auto plays = tracker.getCurrentPlays (600.0);
            expect (plays > 30.0 && plays < 45.0, "plays = " + juce::String (plays));
        }

        beginTest ("Playhead: restarting from the same bar counts each time; a play-through counts once");
        {
            PlayheadTracker tracker;

            for (int restart = 0; restart < 5; ++restart)
            {
                tracker.update ({ true, 40.0, 4.0 }, restart * 10.0);
                tracker.update ({ false, 40.0, 4.0 }, restart * 10.0 + 5.0);
            }

            tracker.update ({ true, 40.0, 4.0 }, 60.0);

            // Six plays, each decayed by e^(−age / 30 min) at t = 60 s.
            double expected = 0.0;

            for (double t : { 0.0, 10.0, 20.0, 30.0, 40.0, 60.0 })
                expected += std::exp (-(60.0 - t) / model::barPlaysDecaySeconds);

            expectWithinAbsoluteError (tracker.getCurrentPlays (60.0), expected, 1.0e-6);

            PlayheadTracker once;

            for (double ppq = 0.0; ppq < 400.0; ppq += 0.5)
                once.update ({ true, ppq, 4.0 }, ppq);

            expectWithinAbsoluteError (once.getCurrentPlays (400.0), 1.0, 0.01);

            once.update ({ false, 0.0, 4.0 }, 401.0);
            expectEquals (once.getCurrentPlays (401.0), 0.0);
        }

        beginTest ("Level creep: the last 10 min vs. an hour ago");
        {
            LevelCreepTracker creep;
            expect (std::isnan (creep.getCreepDb()));

            for (int s = 0; s < 3600; ++s)
                creep.add (s < 600 ? -14.0f : (s < 3000 ? -12.0f : -10.0f), 1.0, false);

            // The older window spans 9 min at −14 and 1 min at −12 → −13.75 dB.
            expectWithinAbsoluteError (creep.getCreepDb(), 3.75, 0.1);
        }
    }
};

static FactorTests factorTests;

} // namespace breark
