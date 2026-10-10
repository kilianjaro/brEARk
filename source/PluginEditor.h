#pragma once

#include "PluginProcessor.h"

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
    void updateButtons();

    BrearkProcessor& brearkProcessor;
    breark::Snapshot snapshot;

    juce::TextButton setupButton, presetButton, warpButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrearkEditor)
};
