#include "ToastWindow.h"
#include "platform/WindowTweaks.h"

namespace
{
using namespace breark::ui;

constexpr int margin = 16;
constexpr int visibleMs = 20000;
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

    // Up to three lines: coach lines are ≤ 104 characters, ~56 fit on a line.
    g.setFont (fonts.mono (11.5f));
    g.drawFittedText (content.message, area.removeFromTop (48.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);

    g.setColour (colours::white.withAlpha (0.8f));
    g.setFont (fonts.mono (10.5f));
    g.drawText (content.detail, area.removeFromBottom (26.0f).withTrimmedRight (140.0f), juce::Justification::centredLeft, true);
}

//==============================================================================
ToastWindow::ToastWindow()
{
    setWantsKeyboardFocus (false);
    setOpaque (true);
    setLookAndFeel (&look);

    for (auto* button : { &breakButton, &snoozeButton, &notThatLoudButton, &closeButton })
    {
        Look::stylePaperButton (*button);
        addAndMakeVisible (*button);
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

    notThatLoudButton.onClick = [this]
    {
        dismiss(); // first: the callback may show a confirmation in this window

        if (onNotThatLoud != nullptr)
            onNotThatLoud();
    };

    breakButton.setTooltip ("Starts a guided break with a countdown. It ends by itself when you play again for 10 seconds.");
    snoozeButton.setTooltip ("Plugs the sound back in for exactly 10 minutes. After that, the plug comes out again if you're still drained.");
    notThatLoudButton.setTooltip ("Tells brEARk that what plays now is your normal listening level. It can't see your volume knob, so this corrects its estimate from now on.");

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
    breakButton.setVisible (content.offerBreak && ! content.offerSnooze && ! content.offerNotThatLoud);
    snoozeButton.setVisible (content.offerSnooze);
    notThatLoudButton.setVisible (content.offerNotThatLoud);

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
    startTimer (visibleMs);
}

void ToastWindow::dismiss()
{
    stopTimer();
    setVisible (false);
}

void ToastWindow::timerCallback()
{
    dismiss();
}

void ToastWindow::resized()
{
    auto area = getLocalBounds().reduced (margin, 12);
    closeButton.setBounds (area.removeFromTop (22).removeFromRight (28));

    const auto buttonRow = area.removeFromBottom (26).removeFromRight (130);
    breakButton.setBounds (buttonRow);
    snoozeButton.setBounds (buttonRow);
    notThatLoudButton.setBounds (buttonRow);
}

void ToastWindow::paint (juce::Graphics& g)
{
    paintToast (g, getLocalBounds().toFloat(), content, *fonts);
}
