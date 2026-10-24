#include "ToastWindow.h"
#include "Tooltips.h"
#include "platform/WindowTweaks.h"

namespace
{
using namespace breark::ui;

constexpr int margin = 16;
constexpr int rowHeight = 26, rowGap = 8;
constexpr int visibleMs = 6000;          // a message: read it, it goes
constexpr int buttonVisibleMs = 30000;   // BREAK NOW or SNOOZE: time to decide (M11.2)
constexpr int afterHoverMs = 1500;       // the mouse was over it: close soon after it leaves
// A question (the check-in) has no timer: it waits for the answer (M11.2).
} // namespace

void paintToast (juce::Graphics& g, juce::Rectangle<float> bounds, const ToastContent& content, const Fonts& fonts)
{
    g.setColour (content.urgent ? colours::signal : colours::ink);
    g.fillRect (bounds);
    g.setColour (colours::white);

    auto area = bounds.reduced ((float) margin, 12.0f);

    g.setFont (fonts.grotesk (14.0f));
    g.drawText ("brEARk.", area.removeFromTop (22.0f), juce::Justification::centredLeft, false);

    g.setFont (fonts.grotesk (36.0f));
    g.drawText (content.headline, area.removeFromTop (44.0f), juce::Justification::centredLeft, true);

    // The bottom rows hold the buttons (and the detail line); the message gets
    // the rest, up to three lines (coach lines are ≤ 104 characters, ~56 a line).
    auto bottom = area.removeFromBottom ((float) (content.offerSetup ? 2 * rowHeight + rowGap : rowHeight));

    g.setFont (fonts.mono (11.5f));
    g.drawFittedText (content.message, area.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);

    g.setColour (colours::white.withAlpha (0.8f));
    g.setFont (fonts.mono (10.5f));
    g.drawText (content.detail, bottom.removeFromBottom ((float) rowHeight).withTrimmedRight (140.0f), juce::Justification::centredLeft, true);
}

//==============================================================================
ToastWindow::ToastWindow()
{
    setWantsKeyboardFocus (false);
    setOpaque (true);
    setLookAndFeel (&look);

    for (auto* button : { &breakButton, &snoozeButton, &closeButton, &levelButtons[0], &levelButtons[1], &levelButtons[2] })
    {
        Look::stylePaperButton (*button);
        addAndMakeVisible (*button);
    }

    // The session check-in's setup: applies at once, keeps the pop-up open.
    setupSwitch.setColours (colours::ink, colours::paper);
    setupSwitch.setSelectedOutline (colours::paper);
    setupSwitch.setTooltip (breark::ui::tips::setup);
    setupSwitch.onChange = [this] (int index)
    {
        if (onSetup != nullptr)
            onSetup (index);
    };
    addChildComponent (setupSwitch);

    // The check-in: one click answers and closes the pop-up.
    for (int level = 0; level < 3; ++level)
    {
        levelButtons[level].setTooltip (breark::ui::tips::levelAnswer);
        levelButtons[level].onClick = [this, level]
        {
            dismiss();

            if (onLevel != nullptr)
                onLevel (level);
        };
    }

    breakButton.onClick = [this]
    {
        if (onBreak != nullptr)
            onBreak();

        dismiss();
    };

    snoozeButton.onClick = [this]
    {
        if (onSnooze != nullptr)
            onSnooze();

        dismiss();
    };

    breakButton.setTooltip ("Starts a guided break with a countdown. It ends by itself when you play again for 10 seconds.");
    snoozeButton.setTooltip ("Plugs the sound back in for exactly 10 minutes. After that, the plug comes out again if you're still drained.");

    closeButton.onClick = [this] { dismiss(); };
    setSize (width, height);
}

ToastWindow::~ToastWindow()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ToastWindow::show (const ToastContent& newContent)
{
    content = newContent;
    breakButton.setVisible (content.offerBreak && ! content.offerSnooze && ! content.offerLevels);
    snoozeButton.setVisible (content.offerSnooze);

    for (auto& button : levelButtons)
        button.setVisible (content.offerLevels);

    setupSwitch.setSelectedIndex (content.setupSelected);
    setupSwitch.setVisible (content.offerSetup);
    setSize (width, heightFor (content));

    if (! isOnDesktop())
    {
        setAlwaysOnTop (true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
        breark::platform::floatAboveEverything (getWindowHandle());
    }

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userBounds.toNearestInt();
        setTopLeftPosition (area.getRight() - width - margin, area.getY() + margin);
    }

    setVisible (true);
    repaint();

    if (content.offerLevels)
        stopTimer();
    else if (content.closeAfterMs > 0)
        startTimer (content.closeAfterMs);
    else
        startTimer (content.offerBreak || content.offerSnooze ? buttonVisibleMs : visibleMs);
}

void ToastWindow::dismiss()
{
    stopTimer();

    if (! isVisible())
        return;

    setVisible (false);

    if (onClosed != nullptr)
        onClosed();
}

void ToastWindow::timerCallback()
{
    // Being read (the mouse is over it): stay, and close soon after.
    if (isMouseOver (true))
    {
        startTimer (afterHoverMs);
        return;
    }

    dismiss();
}

void ToastWindow::resized()
{
    auto area = getLocalBounds().reduced (margin, 12);
    closeButton.setBounds (area.removeFromTop (22).removeFromRight (28));

    const auto buttonRow = area.removeFromBottom (26).removeFromRight (130);
    breakButton.setBounds (buttonRow);
    snoozeButton.setBounds (buttonRow);

    // The check-in's three answers fill the bottom row; the setup sits above them.
    auto rows = getLocalBounds().reduced (margin, 12);
    auto answers = rows.removeFromBottom (rowHeight);
    rows.removeFromBottom (rowGap);
    setupSwitch.setBounds (rows.removeFromBottom (rowHeight));
    const auto answerWidth = (answers.getWidth() - 2 * 8) / 3;

    for (auto& button : levelButtons)
    {
        button.setBounds (answers.removeFromLeft (answerWidth));
        answers.removeFromLeft (8);
    }
}

void ToastWindow::paint (juce::Graphics& g)
{
    paintToast (g, getLocalBounds().toFloat(), content, *fonts);
}
