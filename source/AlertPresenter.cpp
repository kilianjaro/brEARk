#include "AlertPresenter.h"
#include "ui/HistoryView.h"

#include <BinaryData.h>

namespace
{
using breark::AlertKind;
using breark::Tone;
using breark::ui::separator;

// M11: the check-in's question, from why it asks and the level you set.
juce::String checkInQuestion (const breark::Alert& alert, breark::LevelPreset level)
{
    if (alert.checkIn == (int) breark::CheckInPolicy::Reason::firstSound)
        return "A new session. How loud is it on your ears right now?";

    const auto lu = juce::String (juce::roundToInt (std::abs (alert.deviationLu)));
    const auto name = level == breark::LevelPreset::quiet ? "QUIET" : level == breark::LevelPreset::loud ? "LOUD" : "NORMAL";
    return "Your music is " + lu + " LU " + (alert.deviationLu > 0.0 ? "louder" : "quieter")
         + " than when you set " + name + ". How loud is it on your ears now?";
}

// The headline and layout per alert; the message comes from the coach.
ToastContent toastFor (const breark::Alert& alert, const juce::String& message, breark::LevelPreset level, breark::Setup setup)
{
    const auto minutes = juce::String (juce::roundToInt (alert.breakMinutes));
    const auto battery = "EAR BATTERY " + juce::String (juce::roundToInt (alert.battery)) + " %";
    const auto withBreak = battery + "   BREAK " + minutes + " MIN";

    switch (alert.kind)
    {
        case AlertKind::headsUp:         return { "HEADS UP.", message, battery, false, true, false, false, false, 0, 10000 }; // v1.0: 10 s
        case AlertKind::breakDue:        return { "BREAK.", message, withBreak, false, true };
        case AlertKind::reminder:        return { alert.tone == Tone::brutal ? "STOP." : "BREAK.", message, withBreak, alert.tone == Tone::brutal, true };
        case AlertKind::unplug:          return { "UNPLUGGED.", message, battery, true, false, true };
        case AlertKind::unplugAfterTake: return { "AFTER THIS TAKE.", message, battery, false, false };
        case AlertKind::replugged:       return { "BACK ON.", message, battery, false, false };
        case AlertKind::snoozed:         return { "SNOOZED.", message, battery, false, false };
        case AlertKind::breakStarted:    return { "RESTING.", message, "BREAK " + minutes + " MIN", false, false };
        case AlertKind::breakOver:       return { "WELCOME BACK.", message, battery, false, false };
        case AlertKind::dayDone:         return { "DONE.", message, "HEARING DOSE USED UP" + separator + "NEW DAY AT 05:00", false, false };
        case AlertKind::dayReminder:     return { "DONE.", message, "HEARING DOSE USED UP" + separator + "STILL LISTENING", false, false };
        case AlertKind::dayUnplug:       return { "DONE.", message, "UNPLUGGED UNTIL 05:00", true, false, true };
        case AlertKind::weeklySummary:   return { "LAST WEEK.", breark::ui::describeWeek (alert.week), "DETAILS IN HISTORY (POSTER)", false, false };
        case AlertKind::checkIn:
            if (alert.checkIn == (int) breark::CheckInPolicy::Reason::firstSound) // M11.1: the session check-in asks both
                return { alert.welcome ? "WELCOME." : "NEW SESSION.",
                         alert.welcome ? "brEARk tells you when your ears need a break. Where do you listen, and how loud is it on your ears?"
                                       : "Where do you listen, and how loud is it on your ears?",
                         "", false, false, false, true, true, (int) setup };

            return { "HOW LOUD?", checkInQuestion (alert, level), "", false, false, false, true };
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

// M11.3: the coach's memory of shown lines, so a restart doesn't bring them back early.
juce::File coachMemoryFile()
{
    if (juce::SystemStats::getEnvironmentVariable ("BREARK_STATE_FILE", {}).isNotEmpty())
        return {}; // test tools: nothing to remember

    return breark::persistence::defaultFile().getSiblingFile ("coach-memory.json");
}

// A template next to the user's file, so the format is easy to find. Kept up
// to date (M11.3): it's brEARk's file; your own lines go in coach.txt.
void writeExample()
{
    if (juce::SystemStats::getEnvironmentVariable ("BREARK_STATE_FILE", {}).isNotEmpty())
        return; // test tools: don't write into real folders

    const auto example = AlertPresenter::userLinesFile().getSiblingFile ("coach.example.txt");
    const juce::String text (
        "# brEARk: your own coach lines.\n"
        "# Copy this file to coach.txt (same folder) and edit it. brEARk picks up changes\n"
        "# within ~10 s. Same format as the built-in lines:\n"
        "#   [event/tone]  or  [event] (any tone), then one message per line\n"
        "#   tones: witty (fresh ears), mentor (tired), brutal (ignored)\n"
        "#   your lines are added; a header ending in '!' replaces the built-in section\n"
        "#\n"
        "# Events: headsUp breakDue reminder unplug afterTake backOn snoozed breakStart\n"
        "#         breakOver breakTask breakCare breakThought fact loop late harsh\n"
        "#         squashed dose50 dose100 dayReminder dayUnplug idle\n"
        "# Placeholders: {break} {battery} {dose} {listened} {time} {creep} {loop}\n"
        "#               {looptime} {plays} {psr} {spl}\n"
        "\n"
        "[breakTask]\n"
        "Call someone you like. {break} minutes is enough.\n"
        "\n"
        "# [idle/witty!]\n"
        "# Lines here would replace all built-in witty idle lines.\n");

    if (example.existsAsFile() && example.loadFileAsString() == text)
        return;

    if (example.getParentDirectory().createDirectory())
        example.replaceWithText (text);
}
} // namespace

//==============================================================================
AlertPresenter::AlertPresenter()
    : coach (loadLines())
{
    writeExample();

    if (const auto file = userLinesFile(); file.existsAsFile())
        userLinesModified = file.getLastModificationTime();

    if (const auto file = coachMemoryFile(); file.existsAsFile())
        coach.setMemory (breark::Coach::memoryFromVar (juce::JSON::parse (file)));

    levelAnswersSeen = brain->getLevelAnswers();
    startTimerHz (4);
}

AlertPresenter::~AlertPresenter()
{
    stopTimer();
    saveCoachMemory();
}

void AlertPresenter::saveCoachMemory()
{
    if (! coach.takeMemoryChanged())
        return;

    if (const auto file = coachMemoryFile(); file != juce::File() && file.getParentDirectory().createDirectory())
        file.replaceWithText (juce::JSON::toString (breark::Coach::memoryToVar (coach.getMemory())));
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
    c.loopStrain = s.loopStrain;
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

    if (ticks % 240 == 0) // every ~minute, if a line was shown
        saveCoachMemory();

    const auto snapshot = brain->getSnapshot();
    context = makeContext (snapshot);
    firstRun = snapshot.firstRun;

    for (const auto& alert : brain->takeAlerts())
        present (alert);

    if (const auto hint = coach.update (context, juce::Time::getMillisecondCounterHiRes() / 1000.0))
        showToast ({ hint->headline, hint->text, "", false, hint->offerBreak }, false);

    // The level was set (a check-in answer, the switch in the window or DATA): no
    // question is open any more, including one that arrived in this same tick.
    if (const auto answers = brain->getLevelAnswers(); answers != levelAnswersSeen)
    {
        levelAnswersSeen = answers;
        heldQuestion.reset();

        if (toast != nullptr && toast->isAsking())
            toast->dismiss();
    }
}

void AlertPresenter::present (const breark::Alert& alert, bool test)
{
    const auto message = coach.speakFor (alert, context, juce::Time::getMillisecondCounterHiRes() / 1000.0);

    // v1.0: no pop-up for what you just clicked (SNOOZE, BREAK): the window and
    // its coach line show it.
    if (alert.kind == AlertKind::snoozed || alert.kind == AlertKind::breakStarted)
        return;

    const auto settings = brain->getSettings();
    showToast (toastFor (alert, message, settings.preset, settings.setup), alert.withSound && (settings.sound || test));
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
        toast->onLevel = [this] (int level) { brain->setLevelNow ((breark::LevelPreset) level); };
        toast->onSetup = [this] (int setup)
        {
            auto settings = brain->getSettings();
            settings.setup = (breark::Setup) setup;
            brain->setSettings (settings);
        };
        toast->onClosed = [this]
        {
            if (! heldQuestion.has_value())
                return;

            auto question = *std::exchange (heldQuestion, std::nullopt);
            question.setupSelected = (int) brain->getSettings().setup;
            toast->show (question);
        };
    }

    // A question waits under any other pop-up; a newer question replaces it.
    if (content.offerLevels)
        heldQuestion.reset();
    else if (toast->isAsking())
        heldQuestion = toast->getContent();

    toast->show (content);

    if (withSound)
    {
        if (sound == nullptr)
            sound = std::make_unique<AlertSound> (BinaryData::alert_mp3, (size_t) BinaryData::alert_mp3Size);

        sound->play();
    }
}
