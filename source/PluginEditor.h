#pragma once

#include "PluginProcessor.h"

//==============================================================================
// M1 debug view: raw measurements and performance numbers, refreshed at 4 Hz.
// Replaced by the real COMPACT / POSTER interface in milestone M7.
class BrearkEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit BrearkEditor (BrearkProcessor&);
    ~BrearkEditor() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    BrearkProcessor& brearkProcessor;
    breark::Snapshot snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrearkEditor)
};
