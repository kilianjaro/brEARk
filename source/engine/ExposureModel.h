#pragma once

#include "ModelConstants.h"

#include <juce_core/juce_core.h>

#include <limits>
#include <vector>

namespace breark
{
enum class Setup { monitors, headphones };
enum class LevelPreset { quiet, normal, loud };
enum class Strictness { gentle, escalating, hard };

struct Settings
{
    Setup setup = Setup::monitors;
    LevelPreset preset = LevelPreset::normal;
    Strictness strictness = Strictness::escalating;
};

// One measurement period, handed over by the analysis worker.
struct ListeningPeriod
{
    double seconds = 1.0;                                          // model time covered
    float  lufsShortTerm = -std::numeric_limits<float>::infinity(); // perceived loudness
    float  levelA = -std::numeric_limits<float>::infinity();        // dBFS(A), for the dose
    bool   silent = true;                                          // also while exporting / muted
    bool   onBreak = false;                                        // a BREAK was pressed: recovery starts at once
    double fatigueFactor = 1.0;                                    // M3: harshness · dynamics · repetition · night
};

// One battery sample per minute of the day, for the history view.
struct CurvePoint
{
    int minute = 0;  // minutes since the day started (05:00)
    int battery = 0; // %
    bool off = false; // brEARk was switched off in Live
};

struct DayRecord
{
    int day = 0;            // see ExposureModel::dayNumber()
    double dose = 0.0;      // % of the WHO daily allowance
    double listeningSeconds = 0.0;
    int breaks = 0;
    int powerDowns = 0; // HARD: times the plug was pulled
    int snoozes = 0;
    double offSeconds = 0.0; // switched off in Live
    double longestStretchSeconds = 0.0; // longest listening without a 5 min break (M10)
    int emptyBattery = 0;               // times the battery ran empty (M10)
};

// One week (Monday 05:00 to Monday 05:00), for the weekly summary (M10).
struct WeekSummary
{
    int week = 0;                // see ExposureModel::weekNumber()
    int daysListened = 0;
    double whoWeekPercent = 0.0; // the week's dose against the WHO weekly allowance (5 full days)
    int daysOverDose = 0;        // days at 100 % of the daily dose or more
    double listeningSeconds = 0.0;
    double longestStretchSeconds = 0.0;
    int breaks = 0;              // silences of 5 min or more
    int emptyBattery = 0;
    int powerDowns = 0;
    int snoozes = 0;
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
    int powerDownsToday = 0;
    int snoozesToday = 0;
    double offSecondsToday = 0.0;        // switched off in Live: not measured
    double longestStretchToday = 0.0;    // M10, see DayRecord
    int emptyBatteryToday = 0;
    int lastSummaryWeek = 0;             // the weekly pop-up was shown for this week
    double referenceLufs = model::initialReferenceLufs; // the signal loudness that matches your level setting
    double referencePlaybackSeconds = 0.0; // how much playback the reference has learned from
    bool levelAnchored = false;          // M11: you set the level while music played; the reference stays put
    std::vector<CurvePoint> todayCurve;  // battery, one point per minute the plugin ran
    std::vector<DayRecord> history;      // previous days, oldest first
};

//==============================================================================
// The exposure model: pure and deterministic, no clock or threads inside.
//
//   EAR BATTERY  drains with *perceived* loudness (LUFS → estimated SPL,
//                5 dB exchange rate), recovers exponentially after 60 s of silence.
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

    // brEARk was switched off in Live for this long: nothing drains or
    // recovers, the time is only counted and marked OFF in today's curve.
    void addOffTime (double seconds, juce::Time now);

    // Estimated levels at the listening position for the current settings.
    double perceivedSpl (float lufsShortTerm) const noexcept; // drives the battery
    double doseSpl (float levelA) const noexcept;             // dB(A), drives the dose

    // Fatigue factors × the daily-load factor, capped: the multiplier on the drain.
    double combinedFactor (double fatigueFactor) const noexcept;

    double weeklyDose() const noexcept; // % of the WHO weekly allowance
    double suggestedBreakMinutes() const noexcept;
    bool isRecovering() const noexcept { return state.silenceSeconds >= model::recoveryOnsetSeconds; }

    // M11: "what plays now is QUIET / NORMAL / LOUD on my ears" (a level click
    // or a check-in answer while music plays). The signal loudness now is the
    // anchor; louder or quieter signal counts relative to it until the next
    // anchor. Before the first anchor, the reference learns from your playback.
    void anchorLevel (double lufs) noexcept
    {
        state.referenceLufs = lufs;
        state.levelAnchored = true;
    }

    // dB from your level setting down to the next quieter one (0 at QUIET), for "AT QUIET ~1:40 H".
    double stepDownDb() const noexcept;

    // Forecasts at a steady level (M10). extraDb: "what if it were this much quieter/louder".
    double minutesToEmpty (float lufsShortTerm, double fatigueFactor, double extraDb = 0.0) const noexcept;
    double minutesToFullDose (float levelA, double extraDb = 0.0) const noexcept;
    double minutesToRecover (double targetBattery) const noexcept; // silence from now on

    // Today as a history record, and a week's numbers from the history and today.
    DayRecord todayRecord() const;
    WeekSummary summariseWeek (int week) const;

    // Weeks since 1970 that start on Monday at 05:00 (the day numbers' weekday: Thursday = 0).
    static int weekNumber (int day) noexcept { return (day + 3) / 7; }

    void countEmptyBattery() noexcept { ++state.emptyBatteryToday; }
    void setLastSummaryWeek (int week) noexcept { state.lastSummaryWeek = week; }

    void countPowerDown() noexcept { ++state.powerDownsToday; }
    void countSnooze() noexcept { ++state.snoozesToday; }

    // Days since 1970-01-01 in local time, with the day starting at 05:00.
    static int dayNumber (juce::Time localTime) noexcept;

    // Minutes since the day started (05:00), 0–1439.
    static int minuteOfDay (juce::Time localTime) noexcept;

private:
    double presetSpl() const noexcept;
    double drainPerMinute (double spl, double fatigueFactor) const noexcept;
    double dosePerSecond (double splA) const noexcept;
    void rollOverIfNewDay (juce::Time now);
    void listen (const ListeningPeriod& period);
    void rest (double seconds, bool onBreak) noexcept;
    void recordCurve (juce::Time now, bool off);

    Settings settings;
    ExposureState state;
};

} // namespace breark
