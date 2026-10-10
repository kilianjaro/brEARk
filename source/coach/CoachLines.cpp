#include "CoachLines.h"

namespace breark
{
void CoachLines::add (const juce::String& text)
{
    juce::StringArray* section = nullptr;

    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();

        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;

        if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            auto key = line.substring (1, line.length() - 1).trim().toLowerCase();
            const auto replace = key.endsWithChar ('!');

            if (replace)
                key = key.dropLastCharacters (1).trim();

            section = &sections[key];

            if (replace)
                section->clear();

            continue;
        }

        if (section != nullptr) // lines before the first header are ignored
            section->add (line);
    }
}

const juce::StringArray& CoachLines::get (const juce::String& key) const
{
    static const juce::StringArray none;
    const auto it = sections.find (key.toLowerCase());
    return it != sections.end() ? it->second : none;
}

} // namespace breark
