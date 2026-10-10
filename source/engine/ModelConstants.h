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

//── Fatigue factors (multiply the battery drain) ──────────────────────────────
inline constexpr double factorSmoothingSeconds = 10.0; // Heuristic: inputs are averaged over ~10 s

// Harshness: energy at the ear's sensitivity peak (ISO 226) and in the sharp
// highs, relative to the 250 Hz–2 kHz body (not the total, so bass-heavy
// mixes aren't automatically "smooth").
inline constexpr double presenceToBodyNeutralDb = -14.0; // Heuristic: ≈ −4 dB/oct balanced mix
inline constexpr double presenceToBodyHarshDb   = -6.0;  // Heuristic
inline constexpr double highsToBodyNeutralDb    = -20.0; // Heuristic
inline constexpr double highsToBodyHarshDb      = -12.0; // Heuristic
inline constexpr double harshnessFactorMax      = 1.3;

// Dynamics: PSR = sample peak (3 s) − short-term loudness.
inline constexpr double psrDynamicDb      = 14.0; // Heuristic: no extra fatigue
inline constexpr double psrBrickwalledDb  = 7.0;  // Heuristic: full extra fatigue
inline constexpr double dynamicsFactorMax = 1.2;

// Repetition ("loop blindness"): affects freshness, not the hearing dose.
inline constexpr double barPlaysNeutral        = 10.0; // Heuristic: playhead plays of the current bar (~30 min)
inline constexpr double barPlaysFull           = 60.0;
inline constexpr double barPlaysDecaySeconds   = 30.0 * 60.0;
inline constexpr double loopMinutesNeutral     = 2.0;  // Heuristic: continuous identical audio
inline constexpr double loopMinutesFull        = 12.0;
inline constexpr double repetitionFactorMax    = 1.5;

// Audio loop detection
inline constexpr double loopWindowSeconds      = 4.0;  // this much must match one period earlier
inline constexpr double loopMinLengthSeconds   = 1.5;
inline constexpr double loopMaxLengthSeconds   = 32.0;
inline constexpr float  loopMatchThresholdDb   = 1.0f; // Heuristic: level-invariant mean deviation
inline constexpr float  loopMinMovementDb      = 1.0f; // Heuristic: drones and static noise never "loop"
inline constexpr int    loopSmoothingFrames    = 8;    // ~370 ms: robust to sub-frame misalignment
inline constexpr double loopSearchIntervalSeconds = 2.0;
inline constexpr double loopGraceSeconds = 8.0; // Heuristic: fader moves / quick edits pause, not reset, a loop

// Night: circadian fatigue, ramps up from 22:00, full from 01:00 until the day starts.
inline constexpr double nightStartHour     = 22.0; // Heuristic
inline constexpr double nightRampHours     = 3.0;  // Heuristic
inline constexpr double nightFactorMax     = 1.25;

//── Level creep (coach input, not a factor) ───────────────────────────────────
inline constexpr int creepWindowMinutes = 10;  // compare the last 10 playback minutes …
inline constexpr int creepSpanMinutes   = 60;  // … with the same window one hour ago
inline constexpr double creepMinPlaybackSeconds = 180.0; // each window needs 3 min of playback

//── History ───────────────────────────────────────────────────────────────────
inline constexpr int historyDays = 14;
} // namespace breark::model
