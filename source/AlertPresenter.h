#pragma once

#include "engine/Brain.h"
#include "platform/AlertSound.h"
#include "ui/ToastWindow.h"

//==============================================================================
// One per Live process (juce::SharedResourcePointer): turns the Brain's alerts
// into the toast and the alert sound, whether or not the plugin window is open.
// Message thread only.
class AlertPresenter final : private juce::Timer
{
public:
    AlertPresenter();
    ~AlertPresenter() override;

    void present (const breark::Alert& alert);

private:
    void timerCallback() override;

    juce::SharedResourcePointer<breark::Brain> brain;
    std::unique_ptr<AlertSound> sound;
    std::unique_ptr<ToastWindow> toast;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlertPresenter)
};
