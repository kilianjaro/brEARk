#pragma once

#include "CoachLines.h"
#include "engine/BreakController.h"
#include "engine/FatigueFactors.h"

#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace breark
{
enum class CoachTone { witty, mentor, brutal };

// What the coach knows right now (taken from the Brain's snapshot).
struct CoachContext
{
    double battery = 100.0;
    double breakMinutes = 5.0;
    double dose = 0.0;            // % of today's allowance
    double listenedSeconds = 0.0; // today
    double creepDb = std::numeric_limits<double>::quiet_NaN();
    double loopingSeconds = 0.0;
    int loopBars = 0;
    double barPlays = 0.0;
    double loopStrain = 0.0;      // minutes of the reference loop (FatigueFactors.h)
    double perceivedSpl = -std::numeric_limits<double>::infinity(); // estimated dB, last 2 minutes
    double psr = std::numeric_limits<double>::infinity();
    FatigueFactors factors;
    BreakPhase phase = BreakPhase::listening;
    bool silent = true;
    bool recovering = false;      // 60 s of silence: an automatic break
    juce::Time now;
};

struct Hint
{
    juce::String headline, text;
    bool offerBreak = false;
};

//==============================================================================
// The coach (CONCEPT.md §9). Mostly quiet; speaks at events:
//   - every alert gets a line, in a tone that shifts with severity:
//     witty while fresh, mentor when tired, brutal when ignored;
//   - during breaks, a new task, care tip or ear fact every minute;
//   - hints (level creep, loops, late hours, harsh, squashed, loud, dose)
//     as silent pop-ups: at most one every 20 min, each with its own cooldown;
//   - now and then an idle line when nothing else happened.
// No line repeats until all its siblings were used (a shuffle bag per section).
// Pure: the caller passes the time. Message thread only.
class Coach
{
public:
    explicit Coach (CoachLines lines, int seed = juce::Random::getSystemRandom().nextInt());

    void setLines (CoachLines newLines);

    // The line for an alert; it also becomes the current line.
    juce::String speakFor (const Alert& alert, const CoachContext& context, double nowSeconds);

    // A few times per second. Returns a hint to show as a pop-up, if one is due.
    std::optional<Hint> update (const CoachContext& context, double nowSeconds);

    // The latest line, for the plugin window.
    const juce::String& getLine() const noexcept { return line; }

    // One line for an event, filled in. Empty if the event has no lines.
    juce::String draw (const juce::String& event, CoachTone tone, const CoachContext& context);

    static juce::String fill (const juce::String& text, const CoachContext& context);

private:
    struct Bag
    {
        std::vector<int> order;
        int size = 0, last = -1;
    };

    void say (const juce::String& text, double nowSeconds);

    CoachLines lines;
    juce::Random random;
    std::map<juce::String, Bag> bags;

    juce::String line { "Listening." };
    double lineSeconds = 0.0;
    double startSeconds = -1.0;
    double lastHintSeconds = -1.0e9;
    std::map<juce::String, double> lastHintOfKind;
    int breakTopic = 0;
};

} // namespace breark
