#pragma once

#include "ExposureModel.h"

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
    float presence      = minusInfinity; // 2–5 kHz share of 20 Hz–20 kHz energy, dB
    float highs         = minusInfinity; // 8–16 kHz share, dB
    bool  silent        = true;

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
