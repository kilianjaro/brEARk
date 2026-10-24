#pragma once

#include "ExposureModel.h"

#include <vector>

namespace breark
{
struct BreakInputs
{
    double seconds = 1.0;              // model time
    double battery = 100.0;            // EAR BATTERY %
    bool silent = true;                // nothing playing (or exporting)
    bool recording = false;            // Live is recording: never unplug now
    double suggestedBreakMinutes = 5.0;
    Strictness strictness = Strictness::escalating;
    bool doseFull = false;             // 100 % of today's WHO dose (M10: end of day)
};

enum class AlertKind
{
    headsUp,         // 20 %: start wrapping up (silent banner)
    breakDue,        // empty: take a break
    reminder,        // still listening after the break was due
    unplug,          // HARD: pull the plug now
    unplugAfterTake, // HARD, but Live is recording: unplug when the take is done
    replugged,       // HARD: the minimum break is over, sound is back
    snoozed,         // HARD: 10 more minutes, then the plug comes out again
    breakStarted,    // the BREAK button
    breakOver,       // the guided break's countdown ended
    dayDone,         // M10: 100 % of the daily dose (GENTLE, ESCALATING)
    dayReminder,     // M10: ESCALATING, every 30 min of listening after that
    dayUnplug,       // M10: HARD, unplugged for the rest of the day
    weeklySummary,   // M10: last week's numbers, once a week (from the Brain)
    checkIn          // M11: "How loud is it on your ears now?" (from the Brain)
};

enum class Tone { calm, firm, brutal };

struct Alert
{
    AlertKind kind = AlertKind::headsUp;
    Tone tone = Tone::calm;
    bool withSound = false;
    double battery = 0.0;
    double breakMinutes = 0.0;
    WeekSummary week;       // weeklySummary only
    int checkIn = 0;        // checkIn only: CheckInPolicy::Reason
    double deviationLu = 0.0; // checkIn only: the last 2 minutes against your anchor
};

enum class BreakPhase { listening, warned, due, onBreak, unplugged };

//==============================================================================
// The escalation ladder (docs/ALERTS.md), as a pure state machine:
//
//                 20 %          0 %          +10 min      +20 min      every 15 min
//   GENTLE        ·             ♪ + banner   ·            ·            ·
//   ESCALATING    banner        ♪ calm       ♪ firm       ♪ BRUTAL     ♪ BRUTAL
//   HARD          banner        ♪ UNPLUG (after the take while recording)
//
// The clock after "empty" only runs while you listen; silence pauses it, and
// once the battery has recovered to 30 % everything starts over. While on a
// guided break (the BREAK button) nothing nags; playing for 10 s ends it.
//
// HARD, unplugged: the sound stays off for the minimum break (5 min), battery
// full or not. SNOOZE buys exactly 10 minutes, then the plug comes out again.
// Leaving HARD mode plugs back in at once.
//
// End of day (M10), at 100 % of the daily dose, while you listen: GENTLE and
// ESCALATING say so once (ESCALATING again every 30 min of listening); HARD
// unplugs until the next day starts at 05:00. SNOOZE works the same way.
// Worker thread only.
class BreakController
{
public:
    // The BREAK button: starts a guided break, or ends a running one early.
    void toggleBreak (double suggestedMinutes) noexcept;

    // HARD, unplugged: 10 more minutes.
    void snooze() noexcept { snoozeRequested = true; }

    // Advances by one model tick; returns the alerts to show.
    std::vector<Alert> advance (const BreakInputs& in);

    BreakPhase getPhase() const noexcept { return phase; }
    double getSecondsSinceDue() const noexcept { return secondsSinceDue; }
    double getBreakSecondsLeft() const noexcept { return breakSecondsLeft; }
    bool isUnplugRequested() const noexcept { return phase == BreakPhase::unplugged; }
    double getUnpluggedSeconds() const noexcept { return unpluggedSeconds; }
    double getSnoozeSecondsLeft() const noexcept { return snoozeSecondsLeft; }
    bool isUnpluggedForTheDay() const noexcept { return phase == BreakPhase::unplugged && unpluggedForDay; }

private:
    void startOver() noexcept;
    BreakPhase phaseFromFlags() const noexcept;

    BreakPhase phase = BreakPhase::listening;
    bool warned = false, due = false, deferAnnounced = false;
    bool toggleRequested = false, snoozeRequested = false;
    double requestedBreakMinutes = 0.0;
    double secondsSinceDue = 0.0, nextReminderAt = 0.0, breakSecondsLeft = 0.0;
    double unpluggedSeconds = 0.0, snoozeSecondsLeft = 0.0;
    double soundOnBreakSeconds = 0.0;
    bool unpluggedForDay = false, dayAnnounced = false, dayDeferAnnounced = false;
    double dayListeningSeconds = 0.0, nextDayReminderAt = 0.0;
};

} // namespace breark
