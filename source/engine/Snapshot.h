#pragma once

#include "BreakController.h"
#include "ExposureModel.h"
#include "FatigueFactors.h"

#include <juce_core/juce_core.h>

#include <limits>

namespace breark
{
//==============================================================================
// Everything the UI may show, published by the worker thread.
// Copied by value; levels are dB values, −inf when there is no signal.
struct Snapshot
{
    static constexpr float minusInfinity = -std::numeric_limits<float>::infinity();

    // Measurements
    float lufsMomentary = minusInfinity; // BS.1770, 400 ms
    float lufsShortTerm = minusInfinity; // BS.1770, 3 s
    float peakShortTerm = minusInfinity; // sample peak dBFS, 3 s
    float levelA        = minusInfinity; // A-weighted mean square dBFS, last second
    float presenceToBody = minusInfinity; // 2–5 kHz vs 250 Hz–2 kHz, dB, smoothed ~10 s
    float highsToBody    = minusInfinity; // 8–16 kHz vs 250 Hz–2 kHz, dB, smoothed ~10 s
    float psr            = minusInfinity; // peak − short-term loudness, dB, smoothed ~10 s
    bool  silent         = true;

    // Fatigue factors
    FatigueFactors factors;
    float  drainFactor       = 1.0f; // factors × daily load, capped: what the battery sees
    float  barPlays          = 0.0f; // playhead: recent plays of the current bar
    double loopingSeconds    = 0.0;  // audio: continuous identical repetition
    int    loopBars          = 0;    // length of the current loop, 0 = none
    float  loopBestRhythm    = 0.0f; // diagnostics: rhythm match of the last check (1 = identical)
    float  loopBestShiftDb   = 0.0f; // diagnostics: band-level change, dB
    int    loopBestBars      = 0;
    bool   loopHasGrid       = false; // tempo grid available (transport running)
    float  levelCreepDb      = std::numeric_limits<float>::quiet_NaN(); // last 10 min vs 1 h ago
    bool   transportPlaying  = false;
    bool   transportRecording = false;

    // Exposure model
    float    battery               = 100.0f;        // EAR BATTERY %
    float    doseToday             = 0.0f;          // % of the WHO daily allowance
    float    doseWeek              = 0.0f;          // % of the WHO weekly allowance
    float    perceivedSpl          = minusInfinity; // estimated, drives the battery
    float    doseSpl               = minusInfinity; // estimated dB(A), drives the dose
    float    referenceLufs         = -14.0f;        // your long-term typical loudness
    double   listeningSecondsToday = 0.0;
    double   secondsSinceBreak     = 0.0;
    int      breaksToday           = 0;
    bool     recovering            = false;
    float    suggestedBreakMinutes = 5.0f;
    Settings settings;
    int      timeWarp              = 1;

    // Breaks
    BreakPhase breakPhase       = BreakPhase::listening;
    double     secondsSinceDue  = 0.0; // listening time since the battery ran out
    double     breakSecondsLeft = 0.0; // guided break countdown

    // Engine
    double sampleRate = 0.0;
    int    fftSize    = 0;

    // Export detection
    bool  rendering             = false;
    int   rendersSeen           = 0;
    bool  lastRenderHadHostFlag = false;
    float renderSpeed           = 0.0f; // audio time / wall time over the last second

    // Performance
    float       audioMicrosAverage = 0.0f; // per block, all instances
    float       audioMicrosMax     = 0.0f;
    float       workerLoadPercent  = 0.0f; // % of one core
    float       workerWakeMsAverage = 0.0f; // asked for 50 ms; macOS may coalesce
    float       workerWakeMsMax     = 0.0f;
    juce::int64 samplesDropped     = 0;
    juce::int64 framesAnalysed     = 0;
};

} // namespace breark
