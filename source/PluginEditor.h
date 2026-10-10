#pragma once

#include "PluginProcessor.h"

//==============================================================================
// M0 placeholder: shows that brEARk is loaded and how the host runs it.
// Replaced by the real COMPACT / POSTER interface in milestone M7.
class BrearkEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BrearkEditor (BrearkProcessor&);

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrearkEditor)
};
