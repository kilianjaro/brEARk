#pragma once

#include "coach/Coach.h"
#include "engine/Brain.h"
#include "platform/AlertSound.h"
#include "ui/ToastWindow.h"

//==============================================================================
// One per Live process (juce::SharedResourcePointer): turns the Brain's alerts
// and the coach's hints into the toast and the alert sound, whether or not the
// plugin window is open. Owns the coach. Message thread only.
class AlertPresenter final : private juce::Timer
{
public:
    AlertPresenter();
    ~AlertPresenter() override;

    void present (const breark::Alert& alert);

    // The coach's latest line, for the plugin window.
    juce::String getCoachLine() const { return coach.getLine(); }

    // ~/Library/Application Support/brEARk/coach.txt (your own lines).
    static juce::File userLinesFile();

private:
    void timerCallback() override;
    void showToast (const ToastContent& content, bool withSound);
    void reloadLinesIfChanged();
    breark::CoachContext makeContext (const breark::Snapshot& snapshot) const;

    juce::SharedResourcePointer<breark::Brain> brain;
    breark::Coach coach;
    breark::CoachContext context;
    juce::Time userLinesModified;
    int ticks = 0;

    std::unique_ptr<AlertSound> sound;
    std::unique_ptr<ToastWindow> toast;

    // Test tools (the soak test) set BREARK_QUIET: alerts run, but nothing pops up or plays.
    const bool quiet = juce::SystemStats::getEnvironmentVariable ("BREARK_QUIET", {}).isNotEmpty();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlertPresenter)
};
