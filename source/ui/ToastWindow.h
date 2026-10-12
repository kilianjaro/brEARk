#pragma once

#include "Look.h"

struct ToastContent
{
    juce::String headline;  // one big word: "BREAK.", "HEADS UP.", "STOP."
    juce::String message;   // one or two lines
    juce::String detail;    // small data line
    bool urgent = false;    // signal red instead of black
    bool offerBreak = true; // show the BREAK button
    bool offerSnooze = false; // show SNOOZE instead (HARD, unplugged)
    bool offerNotThatLoud = false; // show NOT THAT LOUD instead (the LOUD pop-up)
};

// The toast's drawing (buttons are separate components), for the window and the render tool.
void paintToast (juce::Graphics& g, juce::Rectangle<float> bounds, const ToastContent& content, const breark::ui::Fonts& fonts);

//==============================================================================
// brEARk's own banner: top right of the main screen, above everything (also
// full-screen Live), on every Space, and never takes keyboard focus away from
// Live. Hides itself after 20 s. Message thread only.
class ToastWindow final : public juce::Component,
                          private juce::Timer
{
public:
    static constexpr int width = 420, height = 166;

    ToastWindow();
    ~ToastWindow() override;

    void show (const ToastContent& newContent);
    void dismiss();

    std::function<void()> onBreak, onSnooze, onNotThatLoud;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ToastContent content;
    breark::ui::Look look;
    juce::SharedResourcePointer<breark::ui::Fonts> fonts;
    juce::TextButton breakButton { "BREAK NOW" }, snoozeButton { "SNOOZE 10 MIN" }, notThatLoudButton { "NOT THAT LOUD" }, closeButton { "X" };
    juce::TooltipWindow tooltips { this, 600 }; // the buttons' explanations, inside the pop-up

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToastWindow)
};
