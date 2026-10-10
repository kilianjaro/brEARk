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

double ExposureModel::presetSpl() const noexcept
{
    const auto base = settings.preset == LevelPreset::quiet ? model::presetSplQuiet
                    : settings.preset == LevelPreset::loud  ? model::presetSplLoud
                                                            : model::presetSplNormal;

    return base + (settings.setup == Setup::headphones ? model::headphoneOffsetDb : 0.0);
}

double ExposureModel::perceivedSpl (float lufsShortTerm) const noexcept
{
    return presetSpl() + ((double) lufsShortTerm - state.referenceLufs);
}

double ExposureModel::doseSpl (float levelA) const noexcept
{
    // Same monitor gain, applied to the A-weighted level: bass-heavy material
    // at the same perceived loudness carries less dose, as in the standards.
    return presetSpl() + ((double) levelA - state.referenceLufs);
}

//==============================================================================
void ExposureModel::advance (const ListeningPeriod& period, juce::Time now)
{
    rollOverIfNewDay (now);

    if (period.seconds <= 0.0)
        return;

    if (period.silent || ! std::isfinite (period.lufsShortTerm))
        rest (period.seconds);
    else
        listen (period);

    if (state.silenceSeconds < model::breakMinimumSeconds)
        state.secondsSinceBreak += period.seconds;
}

void ExposureModel::rest (double seconds) noexcept
{
    const auto before = state.silenceSeconds;
    state.silenceSeconds += seconds;

    // Only silence beyond the onset recovers. exp() is exact for any length.
    const auto recoverySeconds = state.silenceSeconds - std::max (before, model::recoveryOnsetSeconds);

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

    // Battery: time-only drain plus an energy part relative to the reference level.
    const auto timeOnly = 100.0 / model::minutesToEmptyTimeOnly;
    const auto atReference = 100.0 / model::minutesToEmptyAtReference;
    // Fatigue doubles every 5 dB (gentler than the 3 dB rule used for the dose).
    const auto energy = std::pow (2.0, (perceivedSpl (period.lufsShortTerm) - model::batteryReferenceSpl)
                                         / model::batteryExchangeRateDb);

    const auto drainPerMinute = (timeOnly + (atReference - timeOnly) * energy) * combinedFactor (period.fatigueFactor);
    state.battery = std::max (0.0, state.battery - drainPerMinute * dt / 60.0);

    // Dose: A-weighted energy against the WHO daily allowance.
    if (std::isfinite (period.levelA))
        state.doseToday += 100.0 * energyRatio (doseSpl (period.levelA), model::doseReferenceDbA)
                         * dt / model::doseReferenceSecondsPerDay;

    // Hybrid level model: a running average of your playback loudness at first,
    // then a slow (~2 h) follower once enough playback has been heard.
    state.referencePlaybackSeconds += dt;
    const auto timeConstant = std::clamp (state.referencePlaybackSeconds,
                                          model::referenceFastLearningSeconds,
                                          model::referenceTimeConstantSeconds);
    state.referenceLufs += ((double) period.lufsShortTerm - state.referenceLufs) * std::min (1.0, dt / timeConstant);
}

void ExposureModel::rollOverIfNewDay (juce::Time now)
{
    const auto today = dayNumber (now);

    if (state.day == today)
        return;

    if (state.day != 0)
    {
        state.history.push_back ({ state.day, state.doseToday, state.listeningSecondsToday, state.breaksToday });

        if ((int) state.history.size() > model::historyDays)
            state.history.erase (state.history.begin(), state.history.end() - model::historyDays);
    }

    state.day = today;
    state.doseToday = 0.0;
    state.listeningSecondsToday = 0.0;
    state.breaksToday = 0;
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
