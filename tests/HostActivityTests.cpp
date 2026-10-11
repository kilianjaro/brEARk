#include "engine/HostActivity.h"

#include <juce_core/juce_core.h>

namespace breark
{
class HostActivityTests final : public juce::UnitTest
{
public:
    HostActivityTests() : juce::UnitTest ("Host activity and sleep", "brEARk") {}

    void runTest() override
    {
        beginTest ("Audio, switched off, stalled");
        {
            HostActivity host;
            expect (host.getState() == HostState::stalled, "nothing heard yet");

            expect (host.update (4800, 0, 0.1) == HostState::playing);
            expect (host.update (0, 4800, 0.1) == HostState::off);
            expect (host.update (1000, 3800, 0.1) == HostState::off, "mostly bypassed: off");
            expect (host.update (3800, 1000, 0.1) == HostState::playing, "mostly processed: playing");
        }

        beginTest ("Gaps under a second keep the state (coalesced wakes, big buffers); longer ones are a stall");
        {
            HostActivity host;
            host.update (4800, 0, 0.1);

            for (int i = 0; i < 4; ++i)
                expect (host.update (0, 0, 0.25) == HostState::playing);

            expect (host.update (0, 0, 0.25) == HostState::stalled);
            expect (host.update (480, 0, 0.01) == HostState::playing, "audio is back");

            host.update (0, 480, 0.1);
            expect (host.update (0, 0, 0.5) == HostState::off, "a short gap while off stays off");
            expect (host.update (0, 0, 0.6) == HostState::stalled);
        }

        beginTest ("Sleep: wall clock ran on while the monotonic clock stood still");
        {
            SleepDetector sleep;
            const juce::int64 wall = 1760090000000;

            expectEquals (sleep.update (1000.0, wall), 0.0, "first call has nothing to compare");
            expectEquals (sleep.update (1050.0, wall + 50), 0.0);
            expectEquals (sleep.update (1300.0, wall + 300 + 2000), 0.0, "small clock corrections are ignored");
            expectWithinAbsoluteError (sleep.update (1350.0, wall + 2350 + 8 * 3600 * 1000), 8.0 * 3600.0, 1.0e-6);
            expectEquals (sleep.update (1400.0, wall + 2400 + 8 * 3600 * 1000), 0.0, "counted once");
            expectEquals (sleep.update (1450.0, wall), 0.0, "the clock set back: not sleep");
        }
    }
};

static HostActivityTests hostActivityTests;

} // namespace breark
