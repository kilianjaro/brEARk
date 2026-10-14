#pragma once

#include "Look.h"
#include "SegmentedControl.h"

struct ToastContent
{
    juce::String headline;  // one big word: "BREAK.", "HEADS UP.", "STOP."
    juce::String message;   // one or two lines
    juce::String detail;    // small data line
    bool urgent = false;    // signal red instead of black
    bool offerBreak = true; // show the BREAK button
    bool offerSnooze = false; // show SNOOZE instead (HARD, unplugged)
    bool offerLevels = false; // the check-in: QUIET / NORMAL / LOUD instead (M11)
    bool offerSetup = false;  // the session check-in also asks MONITORS / HEADPHONES (M11.1)
    int setupSelected = 0;    // the current setup, highlighted
};

// The toast's drawing (buttons are separate components), for the window and the render tool.
void paintToast (juce::Graphics& g, juce::Rectangle<float> bounds, const ToastContent& content, const breark::ui::Fonts& fonts);

//==============================================================================
// brEARk's own banner: top right of the main screen, above everything (also
// full-screen Live), on every Space, and never takes keyboard focus away from
// Live. M11.2: a message closes itself after 6 s, one with a button (BREAK
// NOW, SNOOZE) after 30 s, but not while the mouse is over it; a question
// (QUIET / NORMAL / LOUD) stays until it's answered or closed. Every answer
// closes it. Message thread only.
class ToastWindow final : public juce::Component,
                          private juce::Timer
{
public:
    static constexpr int width = 420, height = 166; // taller with the setup row: see heightFor()

    static int heightFor (const ToastContent& content) noexcept { return content.offerSetup ? 200 : height; }

    ToastWindow();
    ~ToastWindow() override;

    void show (const ToastContent& newContent);
    void dismiss();

    // Showing a question that waits for its answer (M11.2).
    bool isAsking() const noexcept { return isVisible() && content.offerLevels; }
    const ToastContent& getContent() const noexcept { return content; }

    std::function<void()> onBreak, onSnooze;
    std::function<void()> onClosed; // after it closed: timed out, answered or X
    std::function<void (int)> onLevel; // 0 QUIET, 1 NORMAL, 2 LOUD
    std::function<void (int)> onSetup; // 0 MONITORS, 1 HEADPHONES

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ToastContent content;
    breark::ui::Look look;
    juce::SharedResourcePointer<breark::ui::Fonts> fonts;
    juce::TextButton breakButton { "BREAK NOW" }, snoozeButton { "SNOOZE 10 MIN" }, closeButton { "X" };
    juce::TextButton levelButtons[3] { juce::TextButton { "QUIET" }, juce::TextButton { "NORMAL" }, juce::TextButton { "LOUD" } };
    SegmentedControl setupSwitch { { "MONITORS", "HEADPHONES" } };
    juce::TooltipWindow tooltips { this, 600 }; // the buttons' explanations, inside the pop-up

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToastWindow)
};
