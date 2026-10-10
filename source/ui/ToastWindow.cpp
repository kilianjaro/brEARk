#include "ToastWindow.h"
#include "platform/WindowTweaks.h"

namespace
{
const auto paper  = juce::Colour (0xffededed);
const auto ink    = juce::Colour (0xff0a0a0a);
const auto signalRed = juce::Colour (0xffe10600);

constexpr int width = 420, height = 150, margin = 16;
constexpr int visibleMs = 20000;
} // namespace

ToastWindow::ToastWindow()
{
    setWantsKeyboardFocus (false);
    setOpaque (true);

    for (auto* button : { &breakButton, &closeButton })
    {
        button->setWantsKeyboardFocus (false);
        button->setColour (juce::TextButton::buttonColourId, paper);
        button->setColour (juce::TextButton::textColourOffId, ink);
        button->setColour (juce::ComboBox::outlineColourId, paper);
        addAndMakeVisible (*button);
    }

    breakButton.onClick = [this]
    {
        if (onBreak != nullptr)
            onBreak();

        dismiss();
    };

    closeButton.onClick = [this] { dismiss(); };
    setSize (width, height);
}

ToastWindow::~ToastWindow()
{
    stopTimer();
}

void ToastWindow::show (const ToastContent& newContent)
{
    content = newContent;
    breakButton.setVisible (content.offerBreak);

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
    breakButton.setBounds (area.removeFromBottom (26).removeFromRight (130));
}

void ToastWindow::paint (juce::Graphics& g)
{
    g.fillAll (content.urgent ? signalRed : ink);
    g.setColour (paper);

    auto area = getLocalBounds().reduced (margin, 12);

    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("brEARk.", area.removeFromTop (22), juce::Justification::centredLeft, false);

    g.setFont (juce::Font (juce::FontOptions (38.0f, juce::Font::bold)));
    g.drawText (content.headline, area.removeFromTop (44), juce::Justification::centredLeft, true);

    const auto mono = juce::Font::getDefaultMonospacedFontName();
    g.setFont (juce::Font (juce::FontOptions (mono, 12.0f, juce::Font::plain)));
    g.drawFittedText (content.message, area.removeFromTop (30), juce::Justification::topLeft, 2);

    g.setColour (paper.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (mono, 11.0f, juce::Font::plain)));
    g.drawText (content.detail, area.removeFromBottom (26), juce::Justification::centredLeft, true);
}
