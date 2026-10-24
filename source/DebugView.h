#pragma once

#include "PluginProcessor.h"
#include "ui/Look.h"
#include "ui/SegmentedControl.h"

//==============================================================================
// DATA: every raw measurement, the model's state and the performance numbers,
// plus HELP (the guide on GitHub), TEST ALERT and, in developer builds,
// TIME x60. Opened from the main view.
class DebugView final : public juce::Component,
                        public juce::TooltipClient
{
public:
    static constexpr int width = 720, height = 820;

    explicit DebugView (BrearkProcessor&);

    void update (const breark::Snapshot& newSnapshot);

    std::function<void()> onBack;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The explanation for the row under the mouse.
    juce::String getTooltip() override;

private:
    BrearkProcessor& brearkProcessor;
    juce::SharedResourcePointer<breark::ui::Fonts> fonts;
    breark::Snapshot snapshot;

    SegmentedControl setupSwitch { { "MONITORS", "HEADPHONES" } };
    SegmentedControl presetSwitch { { "QUIET", "NORMAL", "LOUD" } };
    SegmentedControl warpSwitch { { "TIME x1", "x60 TEST" } };
    SegmentedControl strictnessSwitch { { "GENTLE", "ESCALATING", "HARD" } };
    juce::TextButton breakButton { "BREAK" }, snoozeButton { "SNOOZE" }, testAlertButton { "TEST ALERT" }, backButton { "BACK" }, helpButton { "HELP" };
    juce::StringArray rowLabels; // as painted, for the tooltips
    int rowsTop = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DebugView)
};
