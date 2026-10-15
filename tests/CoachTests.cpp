#include "coach/Coach.h"
#include "engine/ModelConstants.h"

#include <juce_core/juce_core.h>

#include <set>

namespace breark
{
namespace
{
CoachLines builtInLines()
{
    CoachLines lines;
    lines.add (juce::File (BREARK_SOURCE_DIR).getChildFile ("assets/coach/lines.txt").loadFileAsString());
    return lines;
}

// Long-ish values, so the length check is honest.
CoachContext sampleContext()
{
    CoachContext c;
    c.battery = 100.0;
    c.breakMinutes = 25.0;
    c.dose = 100.0;
    c.listenedSeconds = 10.0 * 3600.0;
    c.creepDb = 12.5;
    c.loopingSeconds = 1234.0;
    c.loopBars = 16;
    c.barPlays = 123.0;
    c.perceivedSpl = 100.0;
    c.psr = 10.5;
    c.now = juce::Time (2026, 9, 11, 14, 0);
    return c;
}

constexpr int maxLineLength = 104; // two lines of the pop-up (~53 characters each)
} // namespace

class CoachTests final : public juce::UnitTest
{
public:
    CoachTests() : juce::UnitTest ("Coach", "brEARk") {}

    void runTest() override
    {
        const auto lines = builtInLines();

        beginTest ("Parser: sections, any-tone, comments; user lines add; '!' replaces");
        {
            CoachLines parsed;
            parsed.add ("# comment\nignored before any header\n[Idle/Witty]\nOne.\n\n  Two.  \n[fact]\nF.\n");
            expectEquals (parsed.get ("idle/witty").size(), 2);
            expectEquals (parsed.get ("IDLE/WITTY")[1], juce::String ("Two."));
            expectEquals (parsed.get ("fact").size(), 1);

            parsed.add ("[idle/witty]\nThree.\n");
            expectEquals (parsed.get ("idle/witty").size(), 3);

            parsed.add ("[idle/witty!]\nOnly mine.\n");
            expectEquals (parsed.get ("idle/witty").size(), 1);
            expectEquals (parsed.get ("idle/witty")[0], juce::String ("Only mine."));
        }

        beginTest ("About 370 built-in lines (M11.3), and every event has lines for the tones it uses");
        {
            int total = 0;

            for (const auto& [key, section] : lines.all())
                total += section.size();

            expect (total >= 360, "only " + juce::String (total) + " lines");

            Coach coach (lines, 1);
            const auto c = sampleContext();

            for (const auto* event : { "headsUp", "breakDue", "reminder", "afterTake", "backOn", "snoozed", "breakStart",
                                       "breakOver", "breakTask", "breakCare", "breakThought", "fact", "idle" })
                expect (coach.draw (event, CoachTone::mentor, c).isNotEmpty(), juce::String (event) + "/mentor has no lines");

            for (const auto* event : { "dayReminder", "dose100" })
                expect (coach.draw (event, CoachTone::mentor, c).isNotEmpty(), juce::String (event) + "/mentor has no lines");

            for (const auto* event : { "reminder", "unplug", "breakDue", "snoozed", "dayUnplug" })
                expect (coach.draw (event, CoachTone::brutal, c).isNotEmpty(), juce::String (event) + "/brutal has no lines");

            for (const auto* event : { "loop", "late", "harsh", "squashed", "dose50", "dose100", "idle", "backOn" })
            {
                expect (coach.draw (event, CoachTone::witty, c).isNotEmpty(), juce::String (event) + "/witty has no lines");
                expect (coach.draw (event, CoachTone::mentor, c).isNotEmpty(), juce::String (event) + "/mentor has no lines");
            }
        }

        beginTest ("Every line fills all placeholders and fits the pop-up");
        {
            const auto c = sampleContext();

            for (const auto& [key, section] : lines.all())
            {
                for (const auto& text : section)
                {
                    const auto filled = Coach::fill (text, c);
                    expect (! filled.containsAnyOf ("{}"), "unfilled placeholder in [" + key + "]: " + filled);
                    expect (filled.length() <= maxLineLength,
                            "too long (" + juce::String (filled.length()) + ") in [" + key + "]: " + filled);
                }
            }
        }

        beginTest ("No repeats: every line once before any comes back, never twice in a row");
        {
            Coach coach (lines, 7);
            const auto c = sampleContext();
            const auto size = lines.get ("breakTask").size();
            std::set<juce::String> seen;
            juce::String previous;
            auto twiceInARow = false;

            for (int round = 0; round < 3; ++round)
            {
                seen.clear();

                for (int i = 0; i < size; ++i)
                {
                    const auto text = coach.draw ("breakTask", CoachTone::mentor, c);
                    twiceInARow = twiceInARow || text == previous;
                    previous = text;
                    seen.insert (text);
                }

                expectEquals ((int) seen.size(), size);
            }

            expect (! twiceInARow);
        }

        beginTest ("Tone follows severity: brutal reminders, witty idle when fresh, mentor when tired");
        {
            Coach coach (lines, 3);
            auto c = sampleContext();

            Alert brutal;
            brutal.kind = AlertKind::reminder;
            brutal.tone = Tone::brutal;
            const auto line = coach.speakFor (brutal, c, 0.0);
            expect (line == line.toUpperCase(), "brutal reminders are written in capitals: " + line);
            expectEquals (coach.getLine(), line);

            c.battery = 90.0;
            const auto fresh = coach.draw ("idle", CoachTone::witty, c);
            expect (lines.get ("idle/witty").contains (fresh) || fresh.isNotEmpty());
        }

        beginTest ("Hints: at most one pop-up per 20 min, each kind with its own cooldown");
        {
            Coach coach (lines, 5);
            auto c = sampleContext();
            c.dose = 10.0;
            c.silent = false;
            c.battery = 70.0;
            c.factors.harshness = 1.25; // a bright mix
            c.factors.dynamics = 1.18;  // squashed
            c.loopingSeconds = 0.0;     // nothing else applies
            c.barPlays = 0.0;

            expect (! coach.update (c, 0.0).has_value(), "no hints during warm-up");

            const auto first = coach.update (c, 200.0);
            expect (first.has_value() && first->headline == "BRIGHT.", "the more important hint first");

            expect (! coach.update (c, 200.0 + 10.0 * 60.0).has_value(), "20 min gap");

            const auto second = coach.update (c, 200.0 + 21.0 * 60.0);
            expect (second.has_value() && second->headline == "SQUASHED.", "bright is cooling down, so squashed is next");

            const auto third = coach.update (c, 200.0 + 42.0 * 60.0);
            expect (! third.has_value(), "bright (90 min) and squashed (3 h) still cooling down");

            const auto fourth = coach.update (c, 200.0 + 92.0 * 60.0);
            expect (fourth.has_value() && fourth->headline == "BRIGHT.");
        }

        beginTest ("No dB guesses (M11): no LOUD or CREEP pop-ups; LOOP waits for 60 strain minutes (M9)");
        {
            auto quiet = sampleContext();
            quiet.dose = 10.0;
            quiet.silent = false;
            quiet.battery = 70.0;
            quiet.perceivedSpl = 80.0;
            quiet.creepDb = 0.0;
            quiet.loopStrain = 0.0;
            quiet.now = juce::Time (2026, 9, 11, 14, 0);

            {
                Coach coach (lines, 3);
                auto c = quiet;
                c.perceivedSpl = 105.0;
                c.creepDb = 12.0;
                expect (! coach.update (c, 200.0).has_value() && ! coach.update (c, 1600.0).has_value(),
                        "the check-in asks about loudness; no estimated-dB pop-ups");
            }

            {
                Coach coach (lines, 4);
                auto c = quiet;
                c.loopStrain = model::loopHintStrainMinutes - 1.0;
                expect (! coach.update (c, 200.0).has_value(), "59 strain minutes: no pop-up yet");

                c.loopStrain = model::loopHintStrainMinutes;
                const auto loop = coach.update (c, 1600.0);
                expect (loop.has_value() && loop->headline == "LOOP." && ! loop->offerBreak);
                expect (! loop->text.containsIgnoreCase ("help"), "no rescuing from loops");
            }
        }

        beginTest ("Breaks: a thought on the track, a care tip, a thought on the craft, a fact; one a minute, never a pop-up");
        {
            Coach coach (lines, 9);
            auto c = sampleContext();
            c.phase = BreakPhase::onBreak;

            std::set<juce::String> shown;
            int thoughts = 0;

            for (int minute = 0; minute < 8; ++minute)
            {
                expect (! coach.update (c, 1000.0 + minute * 60.0).has_value());
                shown.insert (coach.getLine());
                thoughts += lines.get ("breakThought").contains (coach.getLine()) ? 1 : 0;
            }

            expectEquals ((int) shown.size(), 8);
            expectEquals (thoughts, 2, "every fourth minute");
        }

        beginTest ("M11.3: the coach remembers shown lines across a restart (and through JSON)");
        {
            const auto c = sampleContext();
            const auto size = lines.get ("breakTask").size();
            std::set<juce::String> before;

            Coach first (lines, 3);

            for (int i = 0; i < size / 2; ++i)
                before.insert (first.draw ("breakTask", CoachTone::mentor, c));

            expect (first.takeMemoryChanged());
            expect (! first.takeMemoryChanged(), "reported once");

            // Live restarts: a new coach with another seed, the memory from disk.
            const auto json = juce::JSON::toString (Coach::memoryToVar (first.getMemory()));
            Coach second (lines, 4);
            second.setMemory (Coach::memoryFromVar (juce::JSON::parse (json)));

            std::set<juce::String> after;

            for (int i = 0; i < size - size / 2; ++i)
            {
                const auto text = second.draw ("breakTask", CoachTone::mentor, c);
                expect (before.count (text) == 0, "came back after the restart: " + text);
                after.insert (text);
            }

            expectEquals ((int) (before.size() + after.size()), size, "the whole section, each line once");

            // A user line added later joins the round; edited lines don't break the memory.
            CoachLines more = lines;
            more.add ("[breakTask]\nMy own new prompt.\n");
            second.setLines (more);
            expectEquals (second.draw ("breakTask", CoachTone::mentor, c), juce::String ("My own new prompt."));
        }
    }
};

static CoachTests coachTests;

} // namespace breark
