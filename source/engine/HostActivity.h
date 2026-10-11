#pragma once

#include <juce_core/juce_core.h>

namespace breark
{
//==============================================================================
// What the host did since the worker's last wake, read from two sample
// counters the audio thread keeps.
//
//   playing  audio arrives through processBlock: it is measured
//   off      audio arrives through processBlockBypassed, i.e. brEARk is
//            switched off in Live: the model freezes, the history shows OFF
//   stalled  no audio at all for over a second (audio device stopped, changed
//            or unplugged): nothing reaches the ears, so it counts as silence
//
// Short gaps are normal (coalesced wakes, large device buffers), so the
// previous state holds until the stall threshold. Worker thread only.
enum class HostState { playing, off, stalled };

class HostActivity
{
public:
    static constexpr double stallSeconds = 1.0;

    HostState update (juce::int64 processedSamples, juce::int64 bypassedSamples, double wallSeconds) noexcept
    {
        if (processedSamples > 0 || bypassedSamples > 0)
        {
            secondsWithoutAudio = 0.0;
            state = bypassedSamples > processedSamples ? HostState::off : HostState::playing;
        }
        else if ((secondsWithoutAudio += wallSeconds) > stallSeconds)
        {
            state = HostState::stalled;
        }

        return state;
    }

    HostState getState() const noexcept { return state; }

private:
    HostState state = HostState::stalled; // nothing heard yet
    double secondsWithoutAudio = 0.0;
};

//==============================================================================
// The Mac's sleep, seen between two wakes of the worker: the monotonic clock
// (mach_absolute_time) stops while the Mac sleeps, the wall clock doesn't.
// The difference is time the ears rested, like the time Live was closed.
class SleepDetector
{
public:
    static constexpr double minimumSleepMs = 5000.0; // ignores small wall-clock corrections

    // Seconds slept since the last call, 0 if none.
    double update (double monotonicMs, juce::int64 wallMs) noexcept
    {
        auto slept = 0.0;

        if (lastMonotonicMs >= 0.0)
        {
            const auto gapMs = (double) (wallMs - lastWallMs) - (monotonicMs - lastMonotonicMs);

            if (gapMs > minimumSleepMs)
                slept = gapMs / 1000.0;
        }

        lastMonotonicMs = monotonicMs;
        lastWallMs = wallMs;
        return slept;
    }

private:
    double lastMonotonicMs = -1.0;
    juce::int64 lastWallMs = 0;
};

} // namespace breark
