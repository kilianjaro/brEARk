#pragma once

#include "PluginProcessor.h"
#include "ui/SegmentedControl.h"

//==============================================================================
// Debug view: raw measurements, model state and performance, refreshed at 4 Hz.
// Replaced by the real COMPACT / POSTER interface in milestone M7.
class BrearkEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit BrearkEditor (BrearkProcessor&);
    ~BrearkEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BrearkProcessor& brearkProcessor;
    breark::Snapshot snapshot;

    SegmentedControl setupSwitch { { "MONITORS", "HEADPHONES" } };
    SegmentedControl presetSwitch { { "QUIET ~70", "NORMAL ~80", "LOUD ~90" } };
    SegmentedControl warpSwitch { { "TIME x1", "x60 TEST" } };
    SegmentedControl strictnessSwitch { { "GENTLE", "ESCALATING", "HARD" } };
    juce::TextButton breakButton { "BREAK" }, testAlertButton { "TEST ALERT" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrearkEditor)
};
