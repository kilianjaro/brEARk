#include "ExposureModel.h"

#include <algorithm>
#include <cmath>

namespace breark
{
namespace
{
// Linear energy ratio of a level relative to a reference (equal-energy principle).
double energyRatio (double levelDb, double referenceDb) noexcept
{
    return std::pow (10.0, (levelDb - referenceDb) / 10.0);
}

// Howard Hinnant's days_from_civil: proleptic Gregorian date → days since 1970-01-01.
int daysFromCivil (int y, int m, int d) noexcept
{
    y -= m <= 2 ? 1 : 0;
    const auto era = (y >= 0 ? y : y - 399) / 400;
    const auto yearOfEra = y - era * 400;
    const auto dayOfYear = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const auto dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}
} // namespace

//==============================================================================
int ExposureModel::dayNumber (juce::Time localTime) noexcept
{
    const auto shifted = localTime - juce::RelativeTime::hours (model::dayStartHour);
    return daysFromCivil (shifted.getYear(), shifted.getMonth() + 1, shifted.getDayOfMonth());
}

int ExposureModel::minuteOfDay (juce::Time localTime) noexcept
{
    return ((localTime.getHours() - model::dayStartHour + 24) % 24) * 60 + localTime.getMinutes();
}

double ExposureModel::presetSpl() const noexcept
{
    const auto base = settings.preset == LevelPreset::quiet ? model::presetSplQuiet
                    : settings.preset == LevelPreset::loud  ? model::presetSplLoud
                                                            : model::presetSplNormal;

    return base + (settings.setup == Setup::headphones ? model::headphoneOffsetDb : 0.0);
}

double ExposureModel::perceivedSpl (float lufsShortTerm) const noexcept
{
    return presetSpl() + ((double) lufsShortTerm - state.referenceLufs) + state.monitorOffsetDb;
}

double ExposureModel::doseSpl (float levelA) const noexcept
{
    // Same monitor gain, applied to the A-weighted level: bass-heavy material
    // at the same perceived loudness carries less dose, as in the standards.
    return presetSpl() + ((double) levelA - state.referenceLufs) + state.monitorOffsetDb;
}

//==============================================================================
void ExposureModel::advance (const ListeningPeriod& period, juce::Time now)
{
    rollOverIfNewDay (now);

    if (period.seconds <= 0.0)
        return;

    if (period.silent || ! std::isfinite (period.lufsShortTerm))
        rest (period.seconds, period.onBreak);
    else
        listen (period);

    if (state.silenceSeconds < model::breakMinimumSeconds)
        state.secondsSinceBreak += period.seconds;

    if (! period.silent)
        state.longestStretchToday = std::max (state.longestStretchToday, state.secondsSinceBreak);

    recordCurve (now, false);
}

void ExposureModel::addOffTime (double seconds, juce::Time now)
{
    rollOverIfNewDay (now);

    if (seconds <= 0.0)
        return;

    state.offSecondsToday += seconds;
    recordCurve (now, true);
}

void ExposureModel::recordCurve (juce::Time now, bool off)
{
    const auto minute = minuteOfDay (now);
    const auto battery = juce::roundToInt (state.battery);

    // One point per minute; within a minute the latest state wins.
    if (state.todayCurve.empty() || state.todayCurve.back().minute != minute)
        state.todayCurve.push_back ({ minute, battery, off });
    else
        state.todayCurve.back() = { minute, battery, off };
}

void ExposureModel::rest (double seconds, bool onBreak) noexcept
{
    const auto before = state.silenceSeconds;
    state.silenceSeconds += seconds;

    // Only silence beyond the onset recovers (on a deliberate break: all of it).
    // exp() is exact for any length.
    const auto onset = onBreak ? 0.0 : model::recoveryOnsetSeconds;
    const auto recoverySeconds = state.silenceSeconds - std::max (before, onset);

    if (recoverySeconds > 0.0)
        state.battery = 100.0 - (100.0 - state.battery) * std::exp (-recoverySeconds / model::recoveryTimeConstantSeconds);
}

void ExposureModel::listen (const ListeningPeriod& period)
{
    const auto dt = period.seconds;

    if (state.silenceSeconds >= model::breakMinimumSeconds)
    {
        ++state.breaksToday;
        state.secondsSinceBreak = 0.0;
    }

    state.silenceSeconds = 0.0;
    state.listeningSecondsToday += dt;

    state.battery = std::max (0.0, state.battery - drainPerMinute (perceivedSpl (period.lufsShortTerm), period.fatigueFactor) * dt / 60.0);

    // Dose: A-weighted energy against the WHO daily allowance.
    if (std::isfinite (period.levelA))
        state.doseToday += dosePerSecond (doseSpl (period.levelA)) * dt;

    // Hybrid level model: a running average of your playback loudness at first,
    // then a slow (~2 h) follower once enough playback has been heard.
    state.referencePlaybackSeconds += dt;
    const auto timeConstant = std::clamp (state.referencePlaybackSeconds,
                                          model::referenceFastLearningSeconds,
                                          model::referenceTimeConstantSeconds);
    state.referenceLufs += ((double) period.lufsShortTerm - state.referenceLufs) * std::min (1.0, dt / timeConstant);
}

double ExposureModel::drainPerMinute (double spl, double fatigueFactor) const noexcept
{
    // Battery: time-only drain plus an energy part relative to the reference level.
    const auto timeOnly = 100.0 / model::minutesToEmptyTimeOnly;
    const auto atReference = 100.0 / model::minutesToEmptyAtReference;
    // Fatigue doubles every 5 dB (gentler than the 3 dB rule used for the dose).
    const auto energy = std::pow (2.0, (spl - model::batteryReferenceSpl) / model::batteryExchangeRateDb);

    return (timeOnly + (atReference - timeOnly) * energy) * combinedFactor (fatigueFactor);
}

double ExposureModel::dosePerSecond (double splA) const noexcept
{
    return 100.0 * energyRatio (splA, model::doseReferenceDbA) / model::doseReferenceSecondsPerDay;
}

double ExposureModel::minutesToEmpty (float lufsShortTerm, double fatigueFactor, double extraDb) const noexcept
{
    if (! std::isfinite (lufsShortTerm))
        return std::numeric_limits<double>::infinity();

    return state.battery / drainPerMinute (perceivedSpl (lufsShortTerm) + extraDb, fatigueFactor);
}

double ExposureModel::minutesToFullDose (float levelA, double extraDb) const noexcept
{
    if (! std::isfinite (levelA))
        return std::numeric_limits<double>::infinity();

    return std::max (0.0, 100.0 - state.doseToday) / (dosePerSecond (doseSpl (levelA) + extraDb) * 60.0);
}

double ExposureModel::minutesToRecover (double targetBattery) const noexcept
{
    if (state.battery >= targetBattery)
        return 0.0;

    // The onset still to wait, then exponential recovery towards 100 %.
    const auto onset = std::max (0.0, model::recoveryOnsetSeconds - state.silenceSeconds);
    const auto seconds = model::recoveryTimeConstantSeconds * std::log ((100.0 - state.battery) / (100.0 - targetBattery));
    return (onset + seconds) / 60.0;
}

WeekSummary ExposureModel::summariseWeek (int week) const
{
    WeekSummary summary;
    summary.week = week;

    auto add = [&] (const DayRecord& r)
    {
        if (weekNumber (r.day) != week || r.listeningSeconds <= 0.0)
            return;

        ++summary.daysListened;
        summary.whoWeekPercent += r.dose / model::weeklyAllowanceDays;
        summary.daysOverDose += r.dose >= 100.0 ? 1 : 0;
        summary.listeningSeconds += r.listeningSeconds;
        summary.longestStretchSeconds = std::max (summary.longestStretchSeconds, r.longestStretchSeconds);
        summary.breaks += r.breaks;
        summary.emptyBattery += r.emptyBattery;
        summary.powerDowns += r.powerDowns;
        summary.snoozes += r.snoozes;
    };

    for (const auto& record : state.history)
        add (record);

    add (todayRecord());
    return summary;
}

DayRecord ExposureModel::todayRecord() const
{
    return { state.day, state.doseToday, state.listeningSecondsToday, state.breaksToday, state.powerDownsToday,
             state.snoozesToday, state.offSecondsToday, state.longestStretchToday, state.emptyBatteryToday };
}

void ExposureModel::rollOverIfNewDay (juce::Time now)
{
    const auto today = dayNumber (now);

    if (state.day == today)
        return;

    if (state.day != 0)
    {
        state.history.push_back (todayRecord());

        if ((int) state.history.size() > model::historyDays)
            state.history.erase (state.history.begin(), state.history.end() - model::historyDays);
    }

    state.day = today;
    state.doseToday = 0.0;
    state.listeningSecondsToday = 0.0;
    state.breaksToday = 0;
    state.powerDownsToday = 0;
    state.snoozesToday = 0;
    state.offSecondsToday = 0.0;
    state.longestStretchToday = 0.0;
    state.emptyBatteryToday = 0;
    state.monitorOffsetDb = 0.0; // a new day starts at your normal level
    state.todayCurve.clear();
}

//==============================================================================
double ExposureModel::combinedFactor (double fatigueFactor) const noexcept
{
    const auto dayLoad = 1.0 + model::dailyLoadFactorMax * std::min (state.doseToday / 100.0, 1.0);
    return std::min (model::maxCombinedFactor, fatigueFactor * dayLoad);
}

double ExposureModel::weeklyDose() const noexcept
{
    auto total = state.doseToday;

    for (const auto& record : state.history)
        if (record.day > state.day - 7 && record.day < state.day)
            total += record.dose;

    return total / model::weeklyAllowanceDays;
}

double ExposureModel::suggestedBreakMinutes() const noexcept
{
    // Time for the battery to refill to the target, including the recovery onset,
    // stretched by up to ×1.5 on a heavy day.
    const auto deficit = 100.0 - state.battery;
    const auto target = 100.0 - model::suggestedBreakTargetBattery;
    const auto refill = deficit > target ? model::recoveryTimeConstantSeconds * std::log (deficit / target) : 0.0;
    const auto minutes = (model::recoveryOnsetSeconds + refill) / 60.0
                       * (1.0 + model::dailyLoadFactorMax * std::min (state.doseToday / 100.0, 1.0));

    return std::clamp (minutes, model::suggestedBreakMinMinutes, model::suggestedBreakMaxMinutes);
}

} // namespace breark
