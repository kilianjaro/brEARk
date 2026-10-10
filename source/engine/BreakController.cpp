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
    secondsSinceDue = nextReminderAt = 0.0;
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

    // HARD mode stays unplugged until the ears have recovered (M5 adds the
    // minimum break and the snooze).
    if (phase == BreakPhase::unplugged)
        return alerts;

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

    if (in.strictness == Strictness::hard && secondsSinceDue >= model::hardGraceSeconds)
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
