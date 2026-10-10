#include "BreakController.h"

#include <utility>

namespace breark
{
void BreakController::toggleBreak (double suggestedMinutes) noexcept
{
    toggleRequested = true;
    requestedBreakMinutes = suggestedMinutes;
}

void BreakController::startOver() noexcept
{
    phase = BreakPhase::listening;
    warned = due = deferAnnounced = false;
    secondsSinceDue = nextReminderAt = unpluggedSeconds = snoozeSecondsLeft = 0.0;
}

BreakPhase BreakController::phaseFromFlags() const noexcept
{
    return due ? BreakPhase::due : (warned ? BreakPhase::warned : BreakPhase::listening);
}

std::vector<Alert> BreakController::advance (const BreakInputs& in)
{
    std::vector<Alert> alerts;

    auto alert = [&] (AlertKind kind, Tone tone, bool withSound)
    {
        alerts.push_back ({ kind, tone, withSound, in.battery, in.suggestedBreakMinutes });
    };

    // HARD, unplugged: the sound stays off for the minimum break. Leaving
    // HARD mode is the escape hatch; SNOOZE buys 10 minutes.
    if (phase == BreakPhase::unplugged)
    {
        unpluggedSeconds += in.seconds;
        toggleRequested = false; // already on a (forced) break

        const auto snooze = std::exchange (snoozeRequested, false);

        if (! snooze && in.strictness == Strictness::hard && unpluggedSeconds < model::minimumBreakSeconds)
            return alerts;

        unpluggedSeconds = 0.0;
        deferAnnounced = false;

        if (snooze)
        {
            snoozeSecondsLeft = model::snoozeSeconds;
            phase = BreakPhase::due;
            alert (AlertKind::snoozed, Tone::firm, false);
            return alerts;
        }

        alert (AlertKind::replugged, Tone::calm, false);

        if (in.battery >= model::rearmBattery)
        {
            startOver();
        }
        else
        {
            // Still drained: the ladder runs again from "empty".
            phase = BreakPhase::due;
            secondsSinceDue = 0.0;
            nextReminderAt = model::firstReminderSeconds;
        }

        return alerts;
    }

    snoozeRequested = false; // only meaningful while unplugged
    snoozeSecondsLeft = std::max (0.0, snoozeSecondsLeft - in.seconds);

    // The BREAK button.
    if (std::exchange (toggleRequested, false))
    {
        if (phase == BreakPhase::onBreak)
        {
            breakSecondsLeft = 0.0; // ended early
            phase = phaseFromFlags();
        }
        else
        {
            breakSecondsLeft = requestedBreakMinutes * 60.0;
            phase = BreakPhase::onBreak;
            alerts.push_back ({ AlertKind::breakStarted, Tone::calm, false, in.battery, requestedBreakMinutes });
            return alerts;
        }
    }

    // A guided break: count down, never nag.
    if (phase == BreakPhase::onBreak)
    {
        if ((breakSecondsLeft -= in.seconds) <= 0.0)
        {
            breakSecondsLeft = 0.0;
            alert (AlertKind::breakOver, Tone::calm, false);

            if (in.battery >= model::rearmBattery)
                startOver();
            else
                phase = phaseFromFlags();
        }

        return alerts;
    }

    // Recovered: everything starts over.
    if (in.battery >= model::rearmBattery)
    {
        startOver();
        return alerts;
    }

    // Nothing plays: the ears are resting, the escalation clock pauses.
    if (in.silent)
        return alerts;

    if (! warned && in.battery <= model::warnBattery && in.strictness != Strictness::gentle)
    {
        warned = true;
        alert (AlertKind::headsUp, Tone::calm, false);
    }

    if (! due)
    {
        if (in.battery <= model::emptyBattery)
        {
            due = true;
            secondsSinceDue = 0.0;
            nextReminderAt = model::firstReminderSeconds;
            alert (AlertKind::breakDue, Tone::calm, true);
        }

        phase = phaseFromFlags();
        return alerts;
    }

    // The break is due and you're still listening.
    secondsSinceDue += in.seconds;

    if (in.strictness == Strictness::escalating && secondsSinceDue >= nextReminderAt)
    {
        const auto first = nextReminderAt <= model::firstReminderSeconds;
        alert (AlertKind::reminder, first ? Tone::firm : Tone::brutal, true);
        nextReminderAt += model::reminderIntervalSeconds;
    }

    if (in.strictness == Strictness::hard && secondsSinceDue >= model::hardGraceSeconds && snoozeSecondsLeft <= 0.0)
    {
        if (in.recording)
        {
            // Never cut a take: announce once, unplug when recording stops.
            if (! std::exchange (deferAnnounced, true))
                alert (AlertKind::unplugAfterTake, Tone::firm, false);
        }
        else
        {
            phase = BreakPhase::unplugged;
            alert (AlertKind::unplug, Tone::brutal, true);
            return alerts;
        }
    }

    phase = BreakPhase::due;
    return alerts;
}

} // namespace breark
