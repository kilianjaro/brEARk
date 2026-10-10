#pragma once

#include "ModelConstants.h"

#include <juce_core/juce_core.h>

#include <limits>
#include <vector>

namespace breark
{
enum class Setup { monitors, headphones };
enum class LevelPreset { quiet, normal, loud };

struct Settings
{
    Setup setup = Setup::monitors;
    LevelPreset preset = LevelPreset::normal;
};

// One measurement period, handed over by the analysis worker.
struct ListeningPeriod
{
    double seconds = 1.0;                                          // model time covered
    float  lufsShortTerm = -std::numeric_limits<float>::infinity(); // perceived loudness
    float  levelA = -std::numeric_limits<float>::infinity();        // dBFS(A), for the dose
    bool   silent = true;                                          // also while exporting / muted
    double fatigueFactor = 1.0;                                    // M3: harshness · dynamics · repetition · night
};

struct DayRecord
{
    int day = 0;            // see ExposureModel::dayNumber()
    double dose = 0.0;      // % of the WHO daily allowance
    double listeningSeconds = 0.0;
    int breaks = 0;
};

// Everything that survives a restart of Live.
struct ExposureState
{
    int day = 0;
    double battery = 100.0;              // %
    double doseToday = 0.0;              // % of the daily allowance
    double listeningSecondsToday = 0.0;
    double silenceSeconds = 0.0;         // current continuous silence
    double secondsSinceBreak = 0.0;
    int breaksToday = 0;
    double referenceLufs = model::initialReferenceLufs;
    std::vector<DayRecord> history;      // previous days, oldest first
};

//==============================================================================
// The exposure model: pure and deterministic, no clock or threads inside.
//
//   EAR BATTERY  drains with *perceived* loudness (LUFS → estimated SPL),
//                recovers exponentially after 60 s of continuous silence.
//   DAILY DOSE   accumulates A-weighted energy (WHO, 3 dB exchange rate),
//                never recovers during the day, resets at 05:00.
class ExposureModel
{
public:
    ExposureModel() = default;
    explicit ExposureModel (ExposureState initialState) : state (std::move (initialState)) {}

    void setSettings (Settings newSettings) noexcept { settings = newSettings; }
    const Settings& getSettings() const noexcept { return settings; }
    const ExposureState& getState() const noexcept { return state; }

    // Advances by one period. Any length works: a time-warped tick, or the
    // hours Live was closed (pass silent = true).
    void advance (const ListeningPeriod& period, juce::Time now);

    // Estimated levels at the listening position for the current settings.
    double perceivedSpl (float lufsShortTerm) const noexcept; // drives the battery
    double doseSpl (float levelA) const noexcept;             // dB(A), drives the dose

    double weeklyDose() const noexcept; // % of the WHO weekly allowance
    double suggestedBreakMinutes() const noexcept;
    bool isRecovering() const noexcept { return state.silenceSeconds >= model::recoveryOnsetSeconds; }

    // Days since 1970-01-01 in local time, with the day starting at 05:00.
    static int dayNumber (juce::Time localTime) noexcept;

private:
    double presetSpl() const noexcept;
    void rollOverIfNewDay (juce::Time now);
    void listen (const ListeningPeriod& period);
    void rest (double seconds) noexcept;

    Settings settings;
    ExposureState state;
};

} // namespace breark
