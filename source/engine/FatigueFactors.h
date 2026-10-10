#pragma once

#include <limits>

namespace breark
{
struct FactorInputs
{
    double presenceToBodyDb = -std::numeric_limits<double>::infinity(); // 2–5 kHz vs 250 Hz–2 kHz
    double highsToBodyDb    = -std::numeric_limits<double>::infinity(); // 8–16 kHz vs 250 Hz–2 kHz
    double psrDb            = std::numeric_limits<double>::infinity();  // peak − short-term loudness
    double barPlays         = 0.0;  // playhead: recent plays of the current bar
    double loopingSeconds   = 0.0;  // audio: continuous identical repetition
    double localHour        = 12.0; // 0–24
};

// Each factor multiplies the battery drain; 1.0 = no extra fatigue.
struct FatigueFactors
{
    double harshness = 1.0;  // 1.0–1.3
    double dynamics = 1.0;   // 1.0–1.2
    double repetition = 1.0; // 1.0–1.5
    double night = 1.0;      // 1.0–1.25

    double product() const noexcept { return harshness * dynamics * repetition * night; }
};

// Pure mapping from measurements to factors; constants in ModelConstants.h.
FatigueFactors computeFatigueFactors (const FactorInputs& inputs) noexcept;

} // namespace breark
