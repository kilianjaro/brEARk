#pragma once

#include "ModelConstants.h"

#include <cmath>
#include <limits>

namespace breark
{
//==============================================================================
// M11.2: "what plays now is NORMAL on my ears" listens for about 2 minutes,
// not a moment. A click while music plays starts from the last 30 s of play
// (or the last few seconds at the start of a session), then keeps averaging
// the energy of the next 90 s of play. A quiet intro or a soloed track at the
// moment of the click can't set the level for the rest of the day.
// Worker thread only.
class LevelAnchor
{
public:
    // The click: the loudness and A-weighted level heard before it, over seedSeconds of play.
    void start (double lufs, double levelA, double seedSeconds) noexcept
    {
        energyLufs = energyA = seconds = secondsA = 0.0;
        afterSeconds = 0.0;
        listening = true;
        accumulate (lufs, levelA, seedSeconds);
    }

    // One period of play after the click (silence is not added). Returns
    // true while the anchor is still listening.
    bool add (double lufs, double levelA, double periodSeconds) noexcept
    {
        if (! listening)
            return false;

        accumulate (lufs, levelA, periodSeconds);
        afterSeconds += periodSeconds;
        listening = afterSeconds < model::anchorAfterSeconds;
        return true;
    }

    void cancel() noexcept { listening = false; }

    bool isListening() const noexcept { return listening; }
    double getLufs() const noexcept { return toDb (energyLufs, seconds); }
    double getLevelA() const noexcept { return toDb (energyA, secondsA); }

private:
    void accumulate (double lufs, double levelA, double periodSeconds) noexcept
    {
        if (! (periodSeconds > 0.0) || ! std::isfinite (lufs))
            return;

        energyLufs += std::pow (10.0, lufs / 10.0) * periodSeconds;
        seconds += periodSeconds;

        if (std::isfinite (levelA))
        {
            energyA += std::pow (10.0, levelA / 10.0) * periodSeconds;
            secondsA += periodSeconds;
        }
    }

    static double toDb (double energySeconds, double overSeconds) noexcept
    {
        return overSeconds > 0.0 && energySeconds > 0.0 ? 10.0 * std::log10 (energySeconds / overSeconds)
                                                        : -std::numeric_limits<double>::infinity();
    }

    double energyLufs = 0.0, energyA = 0.0, seconds = 0.0, secondsA = 0.0, afterSeconds = 0.0;
    bool listening = false;
};

} // namespace breark
