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
};

enum class AlertKind
{
    headsUp,         // 20 %: start wrapping up (silent banner)
    breakDue,        // empty: take a break
    reminder,        // still listening after the break was due
    unplug,          // HARD: pull the plug now (the power-down itself is M5)
    unplugAfterTake, // HARD, but Live is recording: unplug when the take is done
    breakStarted,    // the BREAK button
    breakOver        // the guided break's countdown ended
};

enum class Tone { calm, firm, brutal };

struct Alert
{
    AlertKind kind = AlertKind::headsUp;
    Tone tone = Tone::calm;
    bool withSound = false;
    double battery = 0.0;
    double breakMinutes = 0.0;
};

enum class BreakPhase { listening, warned, due, onBreak, unplugged };

//==============================================================================
// The escalation ladder (CONCEPT.md §6), as a pure state machine:
//
//                 20 %          0 %          +10 min      +15 min      every 5 min
//   GENTLE        ·             ♪ + banner   ·            ·            ·
//   ESCALATING    banner        ♪ calm       ♪ firm       ♪ BRUTAL     ♪ BRUTAL
//   HARD          banner        ♪ calm       UNPLUG (deferred while recording)
//
// The clock after "empty" only runs while you listen; silence pauses it, and
// once the battery has recovered to 30 % everything starts over. While on a
// guided break (the BREAK button) nothing nags. Worker thread only.
class BreakController
{
public:
    // The BREAK button: starts a guided break, or ends a running one early.
    void toggleBreak (double suggestedMinutes) noexcept;

    // Advances by one model tick; returns the alerts to show.
    std::vector<Alert> advance (const BreakInputs& in);

    BreakPhase getPhase() const noexcept { return phase; }
    double getSecondsSinceDue() const noexcept { return secondsSinceDue; }
    double getBreakSecondsLeft() const noexcept { return breakSecondsLeft; }
    bool isUnplugRequested() const noexcept { return phase == BreakPhase::unplugged; }

private:
    void startOver() noexcept;
    BreakPhase phaseFromFlags() const noexcept;

    BreakPhase phase = BreakPhase::listening;
    bool warned = false, due = false, deferAnnounced = false;
    bool toggleRequested = false;
    double requestedBreakMinutes = 0.0;
    double secondsSinceDue = 0.0, nextReminderAt = 0.0, breakSecondsLeft = 0.0;
};

} // namespace breark
