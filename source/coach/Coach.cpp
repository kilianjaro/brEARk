#include "Coach.h"
#include "engine/ModelConstants.h"

#include <cmath>

namespace breark
{
namespace
{
constexpr double warmUpSeconds = 120.0;           // let the measurements settle first
constexpr double hintGapSeconds = 20.0 * 60.0;    // at most one hint pop-up per 20 min
constexpr double breakRotationSeconds = 60.0;     // a new break line every minute
constexpr double idleSeconds = 12.0 * 60.0;       // an idle line after 12 quiet minutes

const char* toneName (CoachTone tone)
{
    switch (tone)
    {
        case CoachTone::witty:  return "witty";
        case CoachTone::mentor: return "mentor";
        case CoachTone::brutal: return "brutal";
    }

    return "mentor";
}

// Witty while fresh, mentor when tired.
CoachTone toneFor (const CoachContext& context)
{
    return context.battery > 50.0 ? CoachTone::witty : CoachTone::mentor;
}

juce::String clock (double seconds, bool hours)
{
    const auto total = (int) seconds;
    return hours ? juce::String (total / 3600) + ":" + juce::String ((total / 60) % 60).paddedLeft ('0', 2)
                 : juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

struct HintRule
{
    const char* event;
    const char* headline;
    double cooldownSeconds;
    bool offerBreak;
    bool (*applies) (const CoachContext&);
};

// Most important first.
const HintRule hintRules[] = {
    { "loud",     "LOUD.",      45.0 * 60.0,   false, [] (const CoachContext& c) { return c.perceivedSpl >= model::loudHintSpl; } },
    { "creep",    "CREEP.",     60.0 * 60.0,   false, [] (const CoachContext& c) { return c.creepDb >= 3.0; } },
    { "loop",     "LOOP.",      60.0 * 60.0,   false, [] (const CoachContext& c) { return c.loopStrain >= model::loopHintStrainMinutes; } },
    { "late",     "LATE.",      6.0 * 3600.0,  false, [] (const CoachContext& c) { const auto h = c.now.getHours(); return h >= 23 || h < 5; } },
    { "harsh",    "BRIGHT.",    90.0 * 60.0,   false, [] (const CoachContext& c) { return c.factors.harshness >= 1.2; } },
    { "squashed", "SQUASHED.",  3.0 * 3600.0,  false, [] (const CoachContext& c) { return c.factors.dynamics >= 1.17; } },
    { "dose50",   "HALF DOSE.", 18.0 * 3600.0, false, [] (const CoachContext& c) { return c.dose >= 50.0 && c.dose < 100.0; } },
};
} // namespace

//==============================================================================
Coach::Coach (CoachLines initialLines, int seed)
    : lines (std::move (initialLines)), random (seed)
{
}

void Coach::setLines (CoachLines newLines)
{
    lines = std::move (newLines);
    bags.clear();
}

juce::String Coach::fill (const juce::String& text, const CoachContext& c)
{
    if (! text.containsChar ('{'))
        return text;

    return text.replace ("{break}", juce::String (juce::roundToInt (c.breakMinutes)))
               .replace ("{battery}", juce::String (juce::roundToInt (c.battery)))
               .replace ("{dose}", juce::String (juce::roundToInt (c.dose)))
               .replace ("{listened}", clock (c.listenedSeconds, true))
               .replace ("{time}", c.now.formatted ("%H:%M"))
               .replace ("{creep}", std::isfinite (c.creepDb) ? juce::String (c.creepDb, 1) : juce::String ("a few"))
               .replace ("{loop}", c.loopBars > 0 ? juce::String (c.loopBars) : juce::String ("few"))
               .replace ("{looptime}", c.loopingSeconds > 0.0 ? clock (c.loopingSeconds, false) : juce::String ("a while"))
               .replace ("{plays}", c.barPlays >= 1.0 ? juce::String (juce::roundToInt (c.barPlays)) : juce::String ("many"))
               .replace ("{psr}", std::isfinite (c.psr) ? juce::String (c.psr, 1) : juce::String ("low"))
               .replace ("{spl}", std::isfinite (c.perceivedSpl) ? juce::String (juce::roundToInt (c.perceivedSpl)) : juce::String ("high"));
}

juce::String Coach::draw (const juce::String& event, CoachTone tone, const CoachContext& context)
{
    // This tone's lines plus the any-tone lines; if there are none, borrow
    // from another tone of the same event.
    auto key = event + "/" + toneName (tone);
    auto candidates = lines.get (key);
    candidates.addArray (lines.get (event));

    for (auto other : { CoachTone::mentor, CoachTone::witty, CoachTone::brutal })
    {
        if (! candidates.isEmpty())
            break;

        key = event + "/" + toneName (other);
        candidates = lines.get (key);
    }

    if (candidates.isEmpty())
        return {};

    // Shuffle bag: every line once before any repeats, and never the same
    // line twice in a row across refills.
    auto& bag = bags[key.toLowerCase()];
    const auto size = candidates.size();

    if (bag.size != size)
        bag = { {}, size, -1 };

    if (bag.order.empty())
    {
        for (int i = 0; i < size; ++i)
            bag.order.push_back (i);

        for (int i = size - 1; i > 0; --i)
            std::swap (bag.order[(size_t) i], bag.order[(size_t) random.nextInt (i + 1)]);

        if (size > 1 && bag.order.back() == bag.last)
            std::swap (bag.order.front(), bag.order.back());
    }

    bag.last = bag.order.back();
    bag.order.pop_back();
    return fill (candidates[bag.last], context);
}

void Coach::say (const juce::String& text, double nowSeconds)
{
    if (text.isNotEmpty())
    {
        line = text;
        lineSeconds = nowSeconds;
    }
}

//==============================================================================
juce::String Coach::speakFor (const Alert& alert, const CoachContext& context, double nowSeconds)
{
    const auto rested = context.battery >= 80.0 ? CoachTone::witty : CoachTone::mentor;
    const auto severity = alert.tone == Tone::brutal ? CoachTone::brutal : CoachTone::mentor;

    juce::String text;

    switch (alert.kind)
    {
        case AlertKind::headsUp:         text = draw ("headsUp", CoachTone::mentor, context); break;
        case AlertKind::breakDue:        text = draw ("breakDue", severity, context); break;
        case AlertKind::reminder:        text = draw ("reminder", severity, context); break;
        case AlertKind::unplug:          text = draw ("unplug", CoachTone::brutal, context); break;
        case AlertKind::unplugAfterTake: text = draw ("afterTake", CoachTone::mentor, context); break;
        case AlertKind::replugged:       text = draw ("backOn", rested, context); break;
        case AlertKind::snoozed:         text = draw ("snoozed", severity, context); break;
        case AlertKind::breakStarted:    text = draw ("breakStart", CoachTone::mentor, context); break;
        case AlertKind::breakOver:       text = draw ("breakOver", rested, context); break;
        case AlertKind::dayDone:         text = draw ("dose100", CoachTone::mentor, context); break;
        case AlertKind::dayReminder:     text = draw ("dayReminder", CoachTone::mentor, context); break;
        case AlertKind::dayUnplug:       text = draw ("dayUnplug", CoachTone::brutal, context); break;
        case AlertKind::weeklySummary:   return {}; // numbers, not a coach line (AlertPresenter)
    }

    say (text, nowSeconds);
    return text;
}

std::optional<Hint> Coach::update (const CoachContext& context, double nowSeconds)
{
    if (startSeconds < 0.0)
        startSeconds = nowSeconds;

    // On a break (guided, unplugged, or just silent for a minute): a new
    // task, care tip or fact every minute. Never a pop-up.
    if (context.phase == BreakPhase::onBreak || context.phase == BreakPhase::unplugged || context.recovering)
    {
        if (nowSeconds - lineSeconds >= breakRotationSeconds)
        {
            static const char* topics[] = { "breakTask", "breakCare", "fact" };
            say (draw (topics[breakTopic++ % 3], CoachTone::mentor, context), nowSeconds);
        }

        return std::nullopt;
    }

    if (context.silent)
        return std::nullopt;

    // Hints: the most important applicable one, if the gap and its cooldown allow.
    if (nowSeconds - startSeconds >= warmUpSeconds && nowSeconds - lastHintSeconds >= hintGapSeconds)
    {
        for (const auto& rule : hintRules)
        {
            const auto it = lastHintOfKind.find (rule.event);

            if (! rule.applies (context) || (it != lastHintOfKind.end() && nowSeconds - it->second < rule.cooldownSeconds))
                continue;

            const auto text = draw (rule.event, toneFor (context), context);

            if (text.isEmpty())
                continue;

            lastHintSeconds = lastHintOfKind[rule.event] = nowSeconds;
            say (text, nowSeconds);
            return Hint { rule.headline, text, rule.offerBreak, juce::String (rule.event) == "loud" };
        }
    }

    // Nothing to say for a while: an idle line.
    if (nowSeconds - lineSeconds >= idleSeconds)
        say (draw ("idle", toneFor (context), context), nowSeconds);

    return std::nullopt;
}

} // namespace breark
