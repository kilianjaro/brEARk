#pragma once

#include "ModelConstants.h"

#include <array>
#include <cmath>
#include <limits>

namespace breark
{
//==============================================================================
// Playback loudness now vs. an hour ago: "your level crept up 4 dB".
// Keeps one energy bucket per minute of wall time; only playback counts.
// The monitor knob is invisible, so this only sees digital creep (master
// fader, limiter gain). Input for the coach, not a fatigue factor.
class LevelCreepTracker
{
public:
    void add (float lufsShortTerm, double seconds, bool silent)
    {
        auto& bucket = buckets[(size_t) current];

        if (! silent && std::isfinite (lufsShortTerm))
        {
            bucket.energy += std::pow (10.0, lufsShortTerm / 10.0) * seconds;
            bucket.playbackSeconds += seconds;
        }

        for (secondsInMinute += seconds; secondsInMinute >= 60.0; secondsInMinute -= 60.0)
        {
            current = (current + 1) % (int) buckets.size();
            buckets[(size_t) current] = {};
        }
    }

    // dB difference between the last window and the same window one span ago;
    // NaN until both windows hold enough playback.
    double getCreepDb() const
    {
        const auto recent = windowLevel (0);
        const auto earlier = windowLevel (model::creepSpanMinutes - model::creepWindowMinutes);
        return recent - earlier;
    }

private:
    struct Bucket
    {
        double energy = 0.0;
        double playbackSeconds = 0.0;
    };

    double windowLevel (int minutesAgo) const
    {
        double energy = 0.0, seconds = 0.0;

        for (int m = minutesAgo; m < minutesAgo + model::creepWindowMinutes; ++m)
        {
            const auto& bucket = buckets[(size_t) ((current - m + 2 * (int) buckets.size()) % (int) buckets.size())];
            energy += bucket.energy;
            seconds += bucket.playbackSeconds;
        }

        return seconds >= model::creepMinPlaybackSeconds ? 10.0 * std::log10 (energy / seconds)
                                                         : std::numeric_limits<double>::quiet_NaN();
    }

    std::array<Bucket, model::creepSpanMinutes + 1> buckets {};
    int current = 0;
    double secondsInMinute = 0.0;
};

} // namespace breark
