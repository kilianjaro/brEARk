#include "AlertPresenter.h"

#include <BinaryData.h>

namespace
{
using breark::AlertKind;
using breark::Tone;

// Placeholder lines until the coach arrives (M6).
ToastContent describe (const breark::Alert& alert)
{
    const auto minutes = juce::String (juce::roundToInt (alert.breakMinutes));
    const auto battery = "EAR BATTERY " + juce::String (juce::roundToInt (alert.battery)) + " %";
    const auto withBreak = battery + "   BREAK " + minutes + " MIN";

    switch (alert.kind)
    {
        case AlertKind::headsUp:
            return { "HEADS UP.", "Battery at 20 %. Start wrapping up this idea.", battery, false, true };

        case AlertKind::breakDue:
            return { "BREAK.", "Your ears are empty. Take " + minutes + " min, away from the speakers.", withBreak, false, true };

        case AlertKind::reminder:
            if (alert.tone == Tone::brutal)
                return { "STOP.", "STOP. " + minutes + " MIN. NO AUDIO.", withBreak, true, true };

            return { "BREAK.", "Still listening. Your judgement is drifting. Take " + minutes + " min.", withBreak, false, true };

        case AlertKind::unplug:
            return { "UNPLUGGED.", "You ignored it. Hard mode pulled the plug: 5 min of silence.", battery, true, false, true };

        case AlertKind::replugged:
            return { "BACK ON.", "Five minutes of silence. Listen with fresh ears.", battery, false, false };

        case AlertKind::snoozed:
            return { "SNOOZED.", "10 more minutes. Then the plug comes out again.", battery, false, false };

        case AlertKind::unplugAfterTake:
            return { "AFTER THIS TAKE.", "Finish the take. Then: unplug.", battery, false, false };

        case AlertKind::breakStarted:
            return { "RESTING.", "Break: " + minutes + " min. Leave the room. Notice the silence.", "", false, false };

        case AlertKind::breakOver:
            return { "WELCOME BACK.", "Break done. Listen with fresh ears.", battery, false, false };
    }

    return {};
}
} // namespace

//==============================================================================
AlertPresenter::AlertPresenter()
{
    startTimerHz (4);
}

AlertPresenter::~AlertPresenter()
{
    stopTimer();
}

void AlertPresenter::timerCallback()
{
    for (const auto& alert : brain->takeAlerts())
        present (alert);
}

void AlertPresenter::present (const breark::Alert& alert)
{
    if (toast == nullptr)
    {
        toast = std::make_unique<ToastWindow>();
        toast->onBreak = [this] { brain->toggleBreak(); };
        toast->onSnooze = [this] { brain->snooze(); };
    }

    toast->show (describe (alert));

    if (alert.withSound)
    {
        if (sound == nullptr)
            sound = std::make_unique<AlertSound> (BinaryData::alert_mp3, (size_t) BinaryData::alert_mp3Size);

        sound->play();
    }
}
