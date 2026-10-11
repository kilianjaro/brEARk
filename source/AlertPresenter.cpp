#include "AlertPresenter.h"

#include <BinaryData.h>

namespace
{
using breark::AlertKind;
using breark::Tone;

// The headline and layout per alert; the message comes from the coach.
ToastContent toastFor (const breark::Alert& alert, const juce::String& message)
{
    const auto minutes = juce::String (juce::roundToInt (alert.breakMinutes));
    const auto battery = "EAR BATTERY " + juce::String (juce::roundToInt (alert.battery)) + " %";
    const auto withBreak = battery + "   BREAK " + minutes + " MIN";

    switch (alert.kind)
    {
        case AlertKind::headsUp:         return { "HEADS UP.", message, battery, false, true };
        case AlertKind::breakDue:        return { "BREAK.", message, withBreak, false, true };
        case AlertKind::reminder:        return { alert.tone == Tone::brutal ? "STOP." : "BREAK.", message, withBreak, alert.tone == Tone::brutal, true };
        case AlertKind::unplug:          return { "UNPLUGGED.", message, battery, true, false, true };
        case AlertKind::unplugAfterTake: return { "AFTER THIS TAKE.", message, battery, false, false };
        case AlertKind::replugged:       return { "BACK ON.", message, battery, false, false };
        case AlertKind::snoozed:         return { "SNOOZED.", message, battery, false, false };
        case AlertKind::breakStarted:    return { "RESTING.", message, "BREAK " + minutes + " MIN", false, false };
        case AlertKind::breakOver:       return { "WELCOME BACK.", message, battery, false, false };
    }

    return {};
}

breark::CoachLines loadLines()
{
    breark::CoachLines lines;
    lines.add (juce::String::fromUTF8 (BinaryData::lines_txt, BinaryData::lines_txtSize));

    if (const auto file = AlertPresenter::userLinesFile(); file.existsAsFile())
        lines.add (file.loadFileAsString());

    return lines;
}

// A template next to the user's file, so the format is easy to find.
void writeExampleOnce()
{
    if (juce::SystemStats::getEnvironmentVariable ("BREARK_STATE_FILE", {}).isNotEmpty())
        return; // test tools: don't write into real folders

    const auto example = AlertPresenter::userLinesFile().getSiblingFile ("coach.example.txt");

    if (example.existsAsFile() || ! example.getParentDirectory().createDirectory())
        return;

    example.replaceWithText (
        "# brEARk: your own coach lines.\n"
        "# Copy this file to coach.txt (same folder) and edit it. brEARk picks up changes\n"
        "# within ~10 s. Same format as the built-in lines:\n"
        "#   [event/tone]  or  [event] (any tone), then one message per line\n"
        "#   tones: witty (fresh ears), mentor (tired), brutal (ignored)\n"
        "#   your lines are added; a header ending in '!' replaces the built-in section\n"
        "#\n"
        "# Events: headsUp breakDue reminder unplug afterTake backOn snoozed breakStart\n"
        "#         breakOver breakTask breakCare fact creep loop late harsh squashed\n"
        "#         loud dose50 dose100 idle\n"
        "# Placeholders: {break} {battery} {dose} {listened} {time} {creep} {loop}\n"
        "#               {looptime} {plays} {psr} {spl}\n"
        "\n"
        "[breakTask]\n"
        "Call someone you like. {break} minutes is enough.\n"
        "\n"
        "# [idle/witty!]\n"
        "# Lines here would replace all built-in witty idle lines.\n");
}
} // namespace

//==============================================================================
AlertPresenter::AlertPresenter()
    : coach (loadLines())
{
    writeExampleOnce();

    if (const auto file = userLinesFile(); file.existsAsFile())
        userLinesModified = file.getLastModificationTime();

    startTimerHz (4);
}

AlertPresenter::~AlertPresenter()
{
    stopTimer();
}

juce::File AlertPresenter::userLinesFile()
{
    return breark::persistence::defaultFile().getSiblingFile ("coach.txt");
}

void AlertPresenter::reloadLinesIfChanged()
{
    const auto file = userLinesFile();
    const auto modified = file.existsAsFile() ? file.getLastModificationTime() : juce::Time();

    if (modified != userLinesModified)
    {
        userLinesModified = modified;
        coach.setLines (loadLines());
    }
}

breark::CoachContext AlertPresenter::makeContext (const breark::Snapshot& s) const
{
    breark::CoachContext c;
    c.battery = s.battery;
    c.breakMinutes = s.suggestedBreakMinutes;
    c.dose = s.doseToday;
    c.listenedSeconds = s.listeningSecondsToday;
    c.creepDb = s.levelCreepDb;
    c.loopingSeconds = s.loopingSeconds;
    c.loopBars = s.loopBars;
    c.barPlays = s.barPlays;
    c.perceivedSpl = s.perceivedSpl;
    c.psr = s.psr;
    c.factors = s.factors;
    c.phase = s.breakPhase;
    c.silent = s.silent;
    c.recovering = s.recovering;
    c.now = juce::Time::getCurrentTime();
    return c;
}

void AlertPresenter::timerCallback()
{
    if (++ticks % 40 == 0) // every ~10 s
        reloadLinesIfChanged();

    context = makeContext (brain->getSnapshot());

    for (const auto& alert : brain->takeAlerts())
        present (alert);

    if (const auto hint = coach.update (context, juce::Time::getMillisecondCounterHiRes() / 1000.0))
        showToast ({ hint->headline, hint->text, "", false, hint->offerBreak }, false);
}

void AlertPresenter::present (const breark::Alert& alert)
{
    const auto message = coach.speakFor (alert, context, juce::Time::getMillisecondCounterHiRes() / 1000.0);
    showToast (toastFor (alert, message), alert.withSound);
}

void AlertPresenter::showToast (const ToastContent& content, bool withSound)
{
    if (quiet)
        return;

    if (toast == nullptr)
    {
        toast = std::make_unique<ToastWindow>();
        toast->onBreak = [this] { brain->toggleBreak(); };
        toast->onSnooze = [this] { brain->snooze(); };
    }

    toast->show (content);

    if (withSound)
    {
        if (sound == nullptr)
            sound = std::make_unique<AlertSound> (BinaryData::alert_mp3, (size_t) BinaryData::alert_mp3Size);

        sound->play();
    }
}
