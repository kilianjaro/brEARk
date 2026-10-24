// brEARk day simulator: what a producer sees during a whole day.
//
// Runs the real exposure model, break controller, check-in policy and coach
// second by second through scripted producer days, with the pop-up timing of
// the plugin (6 s messages, 30 s with a button, questions until answered),
// and logs every pop-up, sound and question. It mirrors the Brain's
// orchestration in a simplified form: a UX study tool, not a test of the Brain.
//
// Usage: brEARk_DaySim [scenario number]   (no argument: all scenarios)

#include "coach/Coach.h"
#include "engine/BreakController.h"
#include "engine/CheckIn.h"
#include "engine/ExposureModel.h"
#include "engine/FatigueFactors.h"
#include "engine/LoudnessWindow.h"

#include <BinaryData.h>
#include <juce_core/juce_core.h>

#include <iostream>
#include <map>

using namespace breark;

namespace
{
struct Block
{
    double startHour, endHour;
    float lufs;              // the music's loudness
    float aGap = -5.0f;      // A-weighted level below LUFS
    double presenceDb = -10.0, highsDb = -12.0, psr = 11.0;
    double loopShare = 0.3;  // share of time spent looping 4 bars
};

struct Scenario
{
    juce::String name;
    Settings settings;
    std::vector<Block> blocks;
    std::vector<double> liveRestarts;  // hours at which Live is restarted (a new session)
    double breakComplianceMinutes = 2.0; // how long after BREAK. the producer actually stops
    bool waitsForStop = false;           // ESCALATING: only stops at STOP.
    bool snoozesOnce = false;            // HARD: snoozes the first unplug of every break
    double answerSeconds = 8.0;          // how long until a question is answered
    LevelPreset level = LevelPreset::normal;
};

struct Toast
{
    juce::String headline, text;
    double shownAt = 0.0, closesAt = 0.0; // closesAt < 0: waits for an answer
    bool question = false;
};

juce::String clockFor (juce::Time t) { return t.formatted ("%H:%M:%S"); }

struct Stats
{
    int toasts = 0, sounds = 0, questions = 0, hints = 0;
    double listeningSeconds = 0.0, toastSeconds = 0.0;
    std::map<juce::String, int> byHeadline;
    int maxInAnHour = 0;
    std::vector<double> toastTimes;
};

void run (const Scenario& scenario)
{
    std::cout << "\n════ " << scenario.name << " ════\n";

    CoachLines lines;
    lines.add (juce::String::fromUTF8 (BinaryData::lines_txt, BinaryData::lines_txtSize));
    Coach::Memory coachMemory;
    auto coach = std::make_unique<Coach> (lines, 42);

    ExposureModel model;
    model.setSettings (scenario.settings);
    BreakController breaks;
    auto checkIn = std::make_unique<CheckInPolicy>();
    LoudnessWindow loudWindow { model::loudWindowSeconds };

    const auto dayStart = juce::Time (2026, 9, 26, 8, 0, 0); // Monday 26 Oct 2026, 08:00
    juce::Random random (7);
    Stats stats;
    std::optional<Toast> toast;
    std::vector<Toast> heldQuestions;

    // The producer's plan.
    double stopUntil = -1.0;            // a break the producer is taking, until this sim second
    double plannedBreakAt = -1.0;       // when the producer will stop after BREAK.
    double microPauseUntil = -1.0, nextMicroPause = 0.0;
    size_t nextRestart = 0;
    bool snoozedThisBreak = false, sessionDue = false;
    juce::String lastCoachLine;

    auto show = [&] (double t, juce::Time now, const juce::String& headline, const juce::String& text, bool sound, bool button, bool question)
    {
        if (toast && toast->question && ! question)
            heldQuestions.push_back (*toast);

        toast = Toast { headline, text, t, question ? -1.0 : t + (button ? 30.0 : 6.0), question };
        ++stats.toasts;
        ++stats.byHeadline[headline];
        stats.toastTimes.push_back (t);
        stats.sounds += sound ? 1 : 0;
        stats.questions += question ? 1 : 0;
        std::cout << clockFor (now) << "  " << (sound ? "♪ " : "  ") << headline.paddedRight (' ', 17)
                  << (question ? "[waits] " : button ? "[30 s]  " : "[6 s]   ") << text << "\n";
    };

    const auto endSeconds = 18.0 * 3600.0; // 08:00 → 02:00

    for (double t = 0.0; t < endSeconds; t += 1.0)
    {
        const auto now = dayStart + juce::RelativeTime::seconds (t);
        const auto hour = 8.0 + t / 3600.0;

        if (nextRestart < scenario.liveRestarts.size() && hour >= scenario.liveRestarts[nextRestart])
        {
            ++nextRestart;
            coachMemory = coach->getMemory();
            coach = std::make_unique<Coach> (lines, 42 + (int) nextRestart);
            coach->setMemory (coachMemory);
            checkIn = std::make_unique<CheckInPolicy>();
            checkIn->startSession (model.sessionQuestionDue()); // v1.0: as the Brain does
            std::cout << clockFor (now) << "  ── Live restarted ──\n";
        }

        // Which music, if any.
        const Block* block = nullptr;
        for (const auto& b : scenario.blocks)
            if (hour >= b.startHour && hour < b.endHour)
                block = &b;

        // Short editing pauses while working: 20–50 s every 3–8 min.
        if (block != nullptr && t >= nextMicroPause)
        {
            microPauseUntil = t + 20.0 + random.nextInt (30);
            nextMicroPause = microPauseUntil + 180.0 + random.nextInt (300);
        }

        const auto producerStopped = t < stopUntil || t < microPauseUntil;
        const auto unplugged = breaks.getPhase() == BreakPhase::unplugged;
        const auto playing = block != nullptr && ! producerStopped;
        const auto silent = ! playing || unplugged;

        const auto lufs = playing ? block->lufs : -std::numeric_limits<float>::infinity();
        const auto levelA = playing ? block->lufs + block->aGap : -std::numeric_limits<float>::infinity();

        if (! silent)
        {
            loudWindow.add (lufs, 1.0);
            stats.listeningSeconds += 1.0;
        }
        else if (model.getState().silenceSeconds >= model::breakMinimumSeconds)
        {
            loudWindow.reset();
        }

        FatigueFactors factors;
        if (! silent)
        {
            FactorInputs inputs;
            inputs.presenceToBodyDb = block->presenceDb;
            inputs.highsToBodyDb = block->highsDb;
            inputs.psrDb = block->psr;
            inputs.loopStrainMinutes = block->loopShare * 70.0 * std::min (1.0, std::fmod (t / 60.0, 90.0) / 90.0);
            inputs.localHour = now.getHours() + now.getMinutes() / 60.0;
            factors = computeFatigueFactors (inputs);
        }

        ListeningPeriod period;
        period.seconds = 1.0;
        period.lufsShortTerm = silent ? -std::numeric_limits<float>::infinity() : lufs;
        period.levelA = silent ? -std::numeric_limits<float>::infinity() : levelA;
        period.silent = silent;
        period.fatigueFactor = factors.product();
        period.onBreak = breaks.getPhase() == BreakPhase::onBreak;
        model.advance (period, now);

        BreakInputs in;
        in.seconds = 1.0;
        in.battery = model.getState().battery;
        in.silent = silent;
        in.suggestedBreakMinutes = model.suggestedBreakMinutes();
        in.strictness = scenario.settings.strictness;
        in.doseFull = model.getState().doseToday >= 100.0;

        CoachContext context;
        context.battery = model.getState().battery;
        context.breakMinutes = model.suggestedBreakMinutes();
        context.dose = model.getState().doseToday;
        context.listenedSeconds = model.getState().listeningSecondsToday;
        context.psr = playing ? block->psr : std::numeric_limits<double>::infinity();
        context.factors = factors;
        context.phase = breaks.getPhase();
        context.silent = silent;
        context.recovering = model.isRecovering();
        context.now = now;

        for (const auto& alert : breaks.advance (in))
        {
            const auto text = coach->speakFor (alert, context, t);

            switch (alert.kind)
            {
                case AlertKind::headsUp:   show (t, now, "HEADS UP.", text, false, true, false); toast->closesAt = t + 10.0; break;
                case AlertKind::breakDue:  show (t, now, "BREAK.", text, true, true, false); break;
                case AlertKind::reminder:  show (t, now, alert.tone == Tone::brutal ? "STOP." : "BREAK.", text, true, true, false); break;
                case AlertKind::unplug:    show (t, now, "UNPLUGGED.", text, true, true, false); break;
                case AlertKind::unplugAfterTake: show (t, now, "AFTER THIS TAKE.", text, false, false, false); break;
                case AlertKind::replugged: show (t, now, "BACK ON.", text, false, false, false); break;
                case AlertKind::snoozed:      // v1.0: the window shows it
                case AlertKind::breakStarted: break;
                case AlertKind::breakOver: show (t, now, "WELCOME BACK.", text, false, false, false); break;
                case AlertKind::dayDone:
                case AlertKind::dayReminder: show (t, now, "DONE.", text, true, false, false); break;
                case AlertKind::dayUnplug: show (t, now, "DONE.", text, true, true, false); break;
                case AlertKind::weeklySummary:
                case AlertKind::checkIn: break;
            }

            // The producer's reaction.
            const auto stopFor = std::max (5.0, model.suggestedBreakMinutes()) * 60.0;

            if (alert.kind == AlertKind::breakDue && ! scenario.waitsForStop)
                plannedBreakAt = t + scenario.breakComplianceMinutes * 60.0;

            if (alert.kind == AlertKind::reminder && alert.tone == Tone::brutal && scenario.waitsForStop)
                plannedBreakAt = t + 60.0;

            if (alert.kind == AlertKind::unplug)
            {
                if (scenario.snoozesOnce && ! snoozedThisBreak)
                {
                    snoozedThisBreak = true;
                    breaks.snooze();
                }
                else
                {
                    stopUntil = t + stopFor; // gives in: a real break
                    snoozedThisBreak = false;
                }
            }

            if (alert.kind == AlertKind::dayDone || alert.kind == AlertKind::dayUnplug)
                std::cout << "                   (dose " << juce::roundToInt (model.getState().doseToday) << " %, "
                          << juce::roundToInt (model.getState().listeningSecondsToday / 60.0) << " min listened)\n";
        }

        if (plannedBreakAt >= 0.0 && t >= plannedBreakAt)
        {
            stopUntil = t + std::max (5.0, model.suggestedBreakMinutes()) * 60.0;
            plannedBreakAt = -1.0;
        }

        // Check-ins: the producer answers after a few seconds, with the true level.
        if (const auto due = model.sessionQuestionDue(); t == 0.0 || (due && ! sessionDue))
            checkIn->startSession (due);
        sessionDue = model.sessionQuestionDue();

        const auto reason = checkIn->update (1.0, ! silent, loudWindow.getLufs(), model.getState().referenceLufs);

        if (reason != CheckInPolicy::Reason::none)
            show (t, now, reason == CheckInPolicy::Reason::firstSound ? "NEW SESSION." : "HOW LOUD?",
                  reason == CheckInPolicy::Reason::firstSound ? "Where do you listen, and how loud?"
                                                              : "Music " + juce::String (loudWindow.getLufs() - model.getState().referenceLufs, 1) + " LU off your setting",
                  false, true, true);

        if (toast && toast->question && t - toast->shownAt >= scenario.answerSeconds)
        {
            if (playing)
                model.anchorLevel (lufs, levelA);

            model.noteLevelSet (now.toMilliseconds());
            checkIn->answered();
            stats.toastSeconds += t - toast->shownAt;
            toast.reset();
        }

        // Hints and the coach line.
        if (const auto hint = coach->update (context, t))
        {
            ++stats.hints;
            show (t, now, hint->headline, hint->text, false, false, false);
        }

        // Timed pop-ups close; a question pushed aside comes back.
        if (toast && ! toast->question && t >= toast->closesAt)
        {
            stats.toastSeconds += toast->closesAt - toast->shownAt;
            toast.reset();

            if (! heldQuestions.empty())
            {
                toast = heldQuestions.back();
                heldQuestions.clear();
            }
        }
    }

    // Busiest hour.
    for (size_t i = 0; i < stats.toastTimes.size(); ++i)
    {
        int inHour = 0;
        for (size_t j = i; j < stats.toastTimes.size() && stats.toastTimes[j] < stats.toastTimes[i] + 3600.0; ++j)
            ++inHour;
        stats.maxInAnHour = std::max (stats.maxInAnHour, inHour);
    }

    const auto hours = stats.listeningSeconds / 3600.0;
    std::cout << "── " << juce::String (hours, 1) << " h listened, dose " << juce::roundToInt (model.getState().doseToday)
              << " %: " << stats.toasts << " pop-ups (" << juce::String (stats.toasts / std::max (0.1, hours), 1) << " per listening hour, busiest hour "
              << stats.maxInAnHour << "), " << stats.sounds << " with sound, " << stats.questions << " questions, " << stats.hints << " hints\n   ";

    for (const auto& [headline, count] : stats.byHeadline)
        std::cout << headline << " " << count << "   ";

    std::cout << "\n";
}
} // namespace

int main (int argc, char* argv[])
{
    // Typical work in progress: −10 LUFS, a few dB under it A-weighted, some loops.
    const Block morning { 10.0, 13.0, -10.0f };
    const Block afternoon { 14.0, 18.5, -10.0f };
    const Block evening { 21.0, 23.5, -9.0f };

    std::vector<Scenario> scenarios;

    scenarios.push_back ({ "1  ESCALATING, monitors, NORMAL: a disciplined day (10–13, 14–18:30, 21–23:30), breaks taken 2 min after BREAK.",
                           { Setup::monitors, LevelPreset::normal, Strictness::escalating }, { morning, afternoon, evening }, { 14.0, 21.0 } });

    Block loudBright { 11.0, 19.5, -8.0f, -4.0f, -6.0, -9.0, 8.0, 0.5 };
    scenarios.push_back ({ "2  ESCALATING, headphones: hyperfocus 11–19:30, bright and squashed, ignores BREAK until STOP.",
                           { Setup::headphones, LevelPreset::normal, Strictness::escalating }, { loudBright }, {}, 2.0, true });

    scenarios.push_back ({ "3  HARD, headphones: the same hyperfocus day, snoozes the first unplug of every break",
                           { Setup::headphones, LevelPreset::normal, Strictness::hard }, { loudBright }, {}, 2.0, false, true });

    Block night { 20.0, 26.0, -12.0f };
    scenarios.push_back ({ "4  GENTLE, monitors: night owl 20:00–02:00, breaks taken 5 min after BREAK.",
                           { Setup::monitors, LevelPreset::normal, Strictness::gentle }, { night }, {}, 5.0 });

    scenarios.push_back ({ "5  ESCALATING: a normal day, but Live is restarted 5 times (crashes, switching sets)",
                           { Setup::monitors, LevelPreset::normal, Strictness::escalating }, { morning, afternoon }, { 11.0, 12.0, 14.0, 15.5, 17.0 } });

    for (size_t i = 0; i < scenarios.size(); ++i)
        if (argc < 2 || juce::String (argv[1]).getIntValue() == (int) i + 1)
            run (scenarios[i]);

    return 0;
}
