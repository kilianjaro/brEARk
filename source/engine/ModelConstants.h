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
// Until ~2 h of playback have been heard, the reference is a running average
// (it knows your level within minutes); afterwards it moves with the 2 h constant.
inline constexpr double referenceFastLearningSeconds = 60.0;
inline constexpr float  silenceThresholdLufs = -55.0f;               // Heuristic: inaudible at sane levels

//── Daily / weekly dose: WHO safe listening (ITU-T H.870) ─────────────────────
// Equal-energy principle with a 3 dB exchange rate: +3 dB = half the time.
inline constexpr double doseReferenceDbA = 80.0;                   // Standard
inline constexpr double doseReferenceSecondsPerDay = 8.0 * 3600.0; // Standard: 40 h/week ÷ 5 days
inline constexpr int    weeklyAllowanceDays = 5;                   // Standard: weekly = 5 daily allowances
inline constexpr int    dayStartHour = 5;                          // Heuristic: a day runs 05:00–05:00

//── EAR BATTERY (short-term fatigue, driven by perceived loudness) ────────────
inline constexpr double batteryReferenceSpl = 80.0;       // Heuristic: typical mixing level
inline constexpr double batteryExchangeRateDb = 5.0;      // Heuristic: +5 dB doubles the drain (fatigue, gentler than the 3 dB damage rule)
inline constexpr double minutesToEmptyAtReference = 70.0; // Heuristic: "10 min break per hour"
inline constexpr double minutesToEmptyTimeOnly = 240.0;   // Heuristic: attention fades even at whisper level
inline constexpr double dailyLoadFactorMax = 0.5;         // Heuristic: drain ×1.5 after a full daily dose
inline constexpr double maxCombinedFactor = 1.6;          // Heuristic: all factors together cap at ×1.6 (M3 Live test: ×2 was too harsh)

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
// Neutral = a typical modern master (M3 Live test: −9…−11 / −12…−14 dB).
inline constexpr double presenceToBodyNeutralDb = -10.0; // Heuristic
inline constexpr double presenceToBodyHarshDb   = -4.0;  // Heuristic
inline constexpr double highsToBodyNeutralDb    = -12.0; // Heuristic
inline constexpr double highsToBodyHarshDb      = -6.0;  // Heuristic
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

// Audio loop detection on the host's tempo grid (see GridLoopDetector)
inline constexpr double loopSlotQuarters     = 0.25;  // one FFT per 16th note
inline constexpr int    loopMaxBars          = 16;    // loops of 1–16 whole bars
inline constexpr int    loopMinWindowBars    = 4;     // compare at least 4 bars: LFO level drift averages out
inline constexpr float  loopMinRhythm        = 0.85f; // Heuristic: tests: loops 0.88–1.0 (heavy humanisation 0.88), new material median 0.7
inline constexpr float  loopMaxShiftDb       = 1.5f;  // Heuristic: tests: loops ≤ 0.54 dB, new chords median 2 dB
inline constexpr double loopRhythmMinChangeDb = 1.5; // a band has rhythm if its 16th-to-16th change is ≥ this (RMS); LFOs stay below
inline constexpr int    loopMinMovingBands   = 3;     // fewer rhythmic bands (a drone, a pad) is never a loop
inline constexpr double loopAudibleRangeDb   = 50.0;  // bands this far below the loudest are noise
inline constexpr double loopGraceSeconds     = 8.0;   // Heuristic: quick edits pause, not reset, a loop

// Night: circadian fatigue, ramps up from 22:00, full from 01:00 until the day starts.
inline constexpr double nightStartHour     = 22.0; // Heuristic
inline constexpr double nightRampHours     = 3.0;  // Heuristic
inline constexpr double nightFactorMax     = 1.25;

//── Level creep (coach input, not a factor) ───────────────────────────────────
inline constexpr int creepWindowMinutes = 10;  // compare the last 10 playback minutes …
inline constexpr int creepSpanMinutes   = 60;  // … with the same window one hour ago
inline constexpr double creepMinPlaybackSeconds = 180.0; // each window needs 3 min of playback

//── Break alerts (CONCEPT.md §6) ──────────────────────────────────────────────
inline constexpr double warnBattery  = 20.0; // heads-up (not in GENTLE)
inline constexpr double emptyBattery = 0.5;  // the break is due
inline constexpr double rearmBattery = 30.0; // recovered: alerts start over
inline constexpr double firstReminderSeconds = 10.0 * 60.0; // listening time after empty
inline constexpr double reminderIntervalSeconds = 5.0 * 60.0;
inline constexpr double hardGraceSeconds = 10.0 * 60.0;     // HARD: unplug after this

//── History ───────────────────────────────────────────────────────────────────
inline constexpr int historyDays = 14;
} // namespace breark::model
