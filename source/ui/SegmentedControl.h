#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// A row of mutually exclusive options; the selected one is filled, so it's
// always clear which one is active.
class SegmentedControl final : public juce::Component
{
public:
    explicit SegmentedControl (const juce::StringArray& options)
    {
        for (int i = 0; i < options.size(); ++i)
        {
            auto* button = buttons.add (new juce::TextButton (options[i]));
            button->setWantsKeyboardFocus (false);
            button->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                       | (i < options.size() - 1 ? juce::Button::ConnectedOnRight : 0));
            button->onClick = [this, i]
            {
                setSelectedIndex (i);

                if (onChange != nullptr)
                    onChange (i);
            };

            addAndMakeVisible (button);
        }
    }

    std::function<void (int)> onChange;

    // The same hover explanation on every option.
    void setTooltip (const juce::String& text)
    {
        for (auto* button : buttons)
            button->setTooltip (text);
    }

    // Updates the display only; doesn't call onChange.
    void setSelectedIndex (int index)
    {
        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);
            buttons[i]->setColour (juce::ComboBox::outlineColourId, i == index ? selectedOutline : outline);
        }
    }

    // The selected option's border. On a background that can be ink (the
    // meter's mass), a paper border keeps the ink-filled selection visible.
    void setSelectedOutline (juce::Colour colour)
    {
        selectedOutline = colour;

        for (auto* button : buttons)
            if (button->getToggleState())
                button->setColour (juce::ComboBox::outlineColourId, colour);
    }

    void setColours (juce::Colour ink, juce::Colour paper)
    {
        outline = selectedOutline = ink;

        for (auto* button : buttons)
        {
            button->setColour (juce::TextButton::buttonColourId, paper);
            button->setColour (juce::TextButton::buttonOnColourId, ink);
            button->setColour (juce::TextButton::textColourOffId, ink);
            button->setColour (juce::TextButton::textColourOnId, paper);
            button->setColour (juce::ComboBox::outlineColourId, ink);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto width = area.getWidth() / juce::jmax (1, buttons.size());

        for (auto* button : buttons)
            button->setBounds (button == buttons.getLast() ? area : area.removeFromLeft (width));
    }

private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Colour outline, selectedOutline;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};
