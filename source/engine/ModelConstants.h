#pragma once

//==============================================================================
// Every tunable number of the exposure model, in one place.
//   Standard  = taken from a published standard
//   Heuristic = engineered from studio practice; tune by ear
namespace breark::model
{
//── Level estimate ────────────────────────────────────────────────────────────
// brEARk can't see the monitor knob. A preset states how loud your *typical*
// playback is; deviations from your own long-term average loudness (the
// reference) move the estimate up or down.
inline constexpr double presetSplQuiet  = 70.0; // Heuristic: you can talk over it easily
inline constexpr double presetSplNormal = 80.0; // Heuristic: you raise your voice slightly
inline constexpr double presetSplLoud   = 90.0; // Heuristic: you have to shout
inline constexpr double headphoneOffsetDb = 3.0; // Heuristic: no room, sound at the eardrum

// Hybrid level model: the reference adapts slowly, so a short louder session
// counts as louder, while a whole day at master loudness becomes the new normal.
inline constexpr double initialReferenceLufs = -14.0;               // Heuristic: typical work in progress
inline constexpr double referenceTimeConstantSeconds = 2.0 * 3600.0; // Heuristic: ~2 h of playback
inline constexpr float  silenceThresholdLufs = -55.0f;               // Heuristic: inaudible at sane levels

//── Daily / weekly dose: WHO safe listening (ITU-T H.870) ─────────────────────
// Equal-energy principle with a 3 dB exchange rate: +3 dB = half the time.
inline constexpr double doseReferenceDbA = 80.0;                   // Standard
inline constexpr double doseReferenceSecondsPerDay = 8.0 * 3600.0; // Standard: 40 h/week ÷ 5 days
inline constexpr int    weeklyAllowanceDays = 5;                   // Standard: weekly = 5 daily allowances
inline constexpr int    dayStartHour = 5;                          // Heuristic: a day runs 05:00–05:00

//── EAR BATTERY (short-term fatigue, driven by perceived loudness) ────────────
inline constexpr double batteryReferenceSpl = 80.0;       // Heuristic: typical mixing level
inline constexpr double minutesToEmptyAtReference = 70.0; // Heuristic: "10 min break per hour"
inline constexpr double minutesToEmptyTimeOnly = 240.0;   // Heuristic: attention fades even at whisper level
inline constexpr double dailyLoadFactorMax = 0.5;         // Heuristic: drain ×1.5 after a full daily dose
inline constexpr double maxCombinedFactor = 2.0;          // Heuristic: all factors together cap at ×2

//── Recovery & breaks ─────────────────────────────────────────────────────────
inline constexpr double recoveryOnsetSeconds = 60.0;         // Heuristic: editing pauses don't count
inline constexpr double recoveryTimeConstantSeconds = 300.0; // Heuristic: 5 min → 63 %, 15 min → 95 %
inline constexpr double breakMinimumSeconds = 300.0;         // Heuristic: this much silence is a break
inline constexpr double suggestedBreakTargetBattery = 90.0;  // Heuristic: a break refills to here
inline constexpr double suggestedBreakMinMinutes = 5.0;
inline constexpr double suggestedBreakMaxMinutes = 25.0;

//── History ───────────────────────────────────────────────────────────────────
inline constexpr int historyDays = 14;
} // namespace breark::model
