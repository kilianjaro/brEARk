#include "engine/CheckIn.h"

#include <juce_core/juce_core.h>

#include <limits>

namespace breark
{
class CheckInTests final : public juce::UnitTest
{
public:
    CheckInTests() : juce::UnitTest ("Check-in (How loud is it on your ears?)", "brEARk") {}

    void runTest() override
    {
        using Reason = CheckInPolicy::Reason;
        constexpr auto none = -std::numeric_limits<float>::infinity();

        beginTest ("The first sound of a session: after 10 s of play, once");
        {
            CheckInPolicy policy;
            expect (run (policy, 600.0, false, none, -14.0) == Reason::none, "silence never asks");
            expect (run (policy, 9.0, true, none, -14.0) == Reason::none, "a short preview doesn't count");
            expect (run (policy, 5.0, false, none, -14.0) == Reason::none);
            expect (run (policy, 10.0, true, none, -14.0) == Reason::firstSound);
            expect (run (policy, 3600.0, true, -14.0f, -14.0) == Reason::none, "once per session");
        }

        beginTest ("Setting the level yourself counts as an answer");
        {
            CheckInPolicy policy;
            policy.answered();
            expect (run (policy, 60.0, true, none, -14.0) == Reason::none);
        }

        beginTest ("6 LU or more away from the anchor over 2 minutes of play: louder or quieter, at most every 20 min");
        {
            CheckInPolicy policy;
            policy.answered(); // the session's level is set

            expect (run (policy, 25.0 * 60.0, true, -9.0f, -14.0) == Reason::none, "5 LU: fine");
            expect (run (policy, 1.0, true, -8.0f, -14.0) == Reason::louder);
            expect (run (policy, 19.0 * 60.0, true, -8.0f, -14.0) == Reason::none, "not again within 20 min");
            expect (run (policy, 2.0 * 60.0, true, -8.0f, -14.0) == Reason::louder, "still far off after 20 min");

            CheckInPolicy quiet;
            quiet.answered();
            expect (run (quiet, 25.0 * 60.0, true, -21.0f, -14.0) == Reason::quieter);
        }

        beginTest ("No 2 minutes of play yet (-inf): no judgement");
        {
            CheckInPolicy policy;
            policy.answered();
            expect (run (policy, 30.0 * 60.0, true, none, -14.0) == Reason::none);
        }
    }

private:
    // Advances one second at a time; returns the first reason given, if any.
    static CheckInPolicy::Reason run (CheckInPolicy& policy, double seconds, bool playing, float recent, double anchor)
    {
        for (double t = 0.0; t < seconds; t += 1.0)
            if (const auto reason = policy.update (1.0, playing, recent, anchor); reason != CheckInPolicy::Reason::none)
                return reason;

        return CheckInPolicy::Reason::none;
    }
};

static CheckInTests checkInTests;

} // namespace breark
