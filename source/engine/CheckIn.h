#pragma once

#include <cmath>

namespace breark
{
//==============================================================================
// M11: when to ask "How loud is it on your ears now?" (QUIET / NORMAL / LOUD).
//
//   firstSound  the first time you play in a session (10 s of sound). v1.0:
//               the Brain starts a session at the first music of the day and
//               after 2 hours without sound, not at every restart of Live
//   louder      the signal has been 6 LU or more above your anchor (the
//   quieter     loudness when you last set the level) over 2 minutes of play
//
// At most once every 20 minutes; setting the level yourself counts as an
// answer. Pure and clock-free; the Brain calls it once per model tick.
class CheckInPolicy
{
public:
    enum class Reason { none, firstSound, louder, quieter };

    static constexpr double firstSoundSeconds = 10.0;
    static constexpr double thresholdLu = 6.0;
    static constexpr double minimumGapSeconds = 20.0 * 60.0;

    // recentLufs: loudness over the last 2 minutes of play (−inf until there are 2 minutes).
    Reason update (double seconds, bool playing, float recentLufs, double anchorLufs) noexcept
    {
        sinceLastSeconds += seconds;

        if (! playing)
        {
            soundSeconds = 0.0;
            return Reason::none;
        }

        soundSeconds += seconds;

        if (! askedThisSession)
        {
            if (soundSeconds < firstSoundSeconds)
                return Reason::none;

            askedThisSession = true;
            sinceLastSeconds = 0.0;
            return Reason::firstSound;
        }

        if (! std::isfinite (recentLufs) || sinceLastSeconds < minimumGapSeconds)
            return Reason::none;

        const auto deviation = (double) recentLufs - anchorLufs;

        if (std::abs (deviation) < thresholdLu)
            return Reason::none;

        sinceLastSeconds = 0.0;
        return deviation > 0.0 ? Reason::louder : Reason::quieter;
    }

    // A session starts: ask at its first sound, or (ask = false) not at all.
    void startSession (bool ask) noexcept
    {
        askedThisSession = ! ask;
        soundSeconds = 0.0;
    }

    // You set the level (a click or an answer): that's as good as a check-in.
    void answered() noexcept
    {
        askedThisSession = true;
        sinceLastSeconds = 0.0;
    }

private:
    bool askedThisSession = false;
    double soundSeconds = 0.0;
    double sinceLastSeconds = 0.0;
};

} // namespace breark
