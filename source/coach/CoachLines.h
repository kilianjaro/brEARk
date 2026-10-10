#pragma once

#include <juce_core/juce_core.h>

#include <map>

namespace breark
{
//==============================================================================
// Coach lines, parsed from the plain-text format of assets/coach/lines.txt:
//
//   # comment
//   [event/tone]      or  [event]  (any tone)
//   one message per line
//
// add() is called for the built-in lines, then for the user's coach.txt:
// sections are added to, unless a header ends in '!', which replaces.
// Keys are case-insensitive.
class CoachLines
{
public:
    void add (const juce::String& text);

    const juce::StringArray& get (const juce::String& key) const;
    const std::map<juce::String, juce::StringArray>& all() const noexcept { return sections; }

private:
    std::map<juce::String, juce::StringArray> sections;
};

} // namespace breark
