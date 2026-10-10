#include "FatigueFactors.h"
#include "ModelConstants.h"

#include <algorithm>
#include <cmath>

namespace breark
{
namespace
{
// 0 at `neutral`, 1 at `full`, clamped; works for falling scales too (PSR).
double ramp (double value, double neutral, double full) noexcept
{
    if (! std::isfinite (value))
        return 0.0;

    return std::clamp ((value - neutral) / (full - neutral), 0.0, 1.0);
}
} // namespace

FatigueFactors computeFatigueFactors (const FactorInputs& in) noexcept
{
    using namespace model;

    const auto harsh = std::max (ramp (in.presenceToBodyDb, presenceToBodyNeutralDb, presenceToBodyHarshDb),
                                 ramp (in.highsToBodyDb, highsToBodyNeutralDb, highsToBodyHarshDb));

    const auto squashed = ramp (in.psrDb, psrDynamicDb, psrBrickwalledDb);

    const auto repeated = std::max (ramp (in.barPlays, barPlaysNeutral, barPlaysFull),
                                    ramp (in.loopingSeconds / 60.0, loopMinutesNeutral, loopMinutesFull));

    // Hours since 22:00, wrapping at midnight; night lasts until the day starts at 05:00.
    const auto hoursIntoNight = std::fmod (in.localHour - nightStartHour + 24.0, 24.0);
    const auto nightLength = std::fmod (dayStartHour - nightStartHour + 24.0, 24.0);
    const auto night = hoursIntoNight < nightLength ? std::min (1.0, hoursIntoNight / nightRampHours) : 0.0;

    FatigueFactors factors;
    factors.harshness  = 1.0 + (harshnessFactorMax - 1.0) * harsh;
    factors.dynamics   = 1.0 + (dynamicsFactorMax - 1.0) * squashed;
    factors.repetition = 1.0 + (repetitionFactorMax - 1.0) * repeated;
    factors.night      = 1.0 + (nightFactorMax - 1.0) * night;
    return factors;
}

} // namespace breark
