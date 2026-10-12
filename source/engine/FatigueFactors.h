#pragma once

#include <limits>

namespace breark
{
struct FactorInputs
{
    double presenceToBodyDb = -std::numeric_limits<double>::infinity(); // 2–5 kHz vs 250 Hz–2 kHz
    double highsToBodyDb    = -std::numeric_limits<double>::infinity(); // 8–16 kHz vs 250 Hz–2 kHz
    double psrDb            = std::numeric_limits<double>::infinity();  // peak − short-term loudness
    double loopStrainMinutes = 0.0; // see loopStrainMinutes()
    double localHour        = 12.0; // 0–24
};

// Each factor multiplies the battery drain; 1.0 = no extra fatigue.
struct FatigueFactors
{
    double harshness = 1.0;  // 1.0–1.3
    double dynamics = 1.0;   // 1.0–1.2
    double repetition = 1.0; // 1.0–1.2
    double night = 1.0;      // 1.0–1.25

    double product() const noexcept { return harshness * dynamics * repetition * night; }
};

// Pure mapping from measurements to factors; constants in ModelConstants.h.
FatigueFactors computeFatigueFactors (const FactorInputs& inputs) noexcept;

// How much a loop has worn on you, in minutes of the reference loop
// (4 bars at 124 bpm): plays × log2 (1 + loop seconds).
double loopStrainMinutes (double plays, double loopSeconds) noexcept;

} // namespace breark
