#pragma once

#include "ModelConstants.h"

#include <cmath>
#include <optional>
#include <unordered_map>

namespace breark
{
//==============================================================================
// Counts how often each bar of the arrangement has been played recently:
// a play is counted when the playhead enters a bar, jumps back (loop brace),
// or playback starts. Counts decay over ~30 min. Catches "play from bar 33
// again", which the audio detector can't see. Worker thread only.
class PlayheadTracker
{
public:
    struct Transport
    {
        bool playing = false;
        double ppq = 0.0;               // position in quarter notes
        double barLengthQuarters = 4.0; // 4/4 → 4, 6/8 → 3
    };

    void update (const Transport& transport, double nowSeconds)
    {
        if (! transport.playing || transport.barLengthQuarters <= 0.0)
        {
            currentBar.reset();
            return;
        }

        const auto bar = (long long) std::floor (transport.ppq / transport.barLengthQuarters + 1.0e-9);
        const auto jumpedBack = transport.ppq < lastPpq - 0.5;

        if (! currentBar || *currentBar != bar || jumpedBack)
        {
            auto& entry = bars[bar];
            entry.plays = decayed (entry, nowSeconds) + 1.0;
            entry.lastSeconds = nowSeconds;
            prune (nowSeconds);
        }

        currentBar = bar;
        lastPpq = transport.ppq;
    }

    // Recent plays of the bar under the playhead (0 when stopped).
    double getCurrentPlays (double nowSeconds) const
    {
        if (! currentBar)
            return 0.0;

        const auto it = bars.find (*currentBar);
        return it != bars.end() ? decayed (it->second, nowSeconds) : 0.0;
    }

private:
    struct Entry
    {
        double plays = 0.0;
        double lastSeconds = 0.0;
    };

    static double decayed (const Entry& entry, double nowSeconds)
    {
        return entry.plays * std::exp (-(nowSeconds - entry.lastSeconds) / model::barPlaysDecaySeconds);
    }

    void prune (double nowSeconds)
    {
        if (bars.size() < 4096)
            return;

        for (auto it = bars.begin(); it != bars.end();)
            it = decayed (it->second, nowSeconds) < 0.1 ? bars.erase (it) : std::next (it);
    }

    std::unordered_map<long long, Entry> bars;
    std::optional<long long> currentBar;
    double lastPpq = 0.0;
};

} // namespace breark
