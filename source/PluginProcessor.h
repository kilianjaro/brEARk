#pragma once

#include "AlertPresenter.h"
#include "engine/Brain.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
// brEARk's audio processor.
//
// Audio rule #1 (IMPLEMENTATION.md §1): the audio thread never writes to the
// buffer, so the output is bit-identical to the input. The only planned
// exception is the Hard-mode PowerDown effect (milestone M5).
class BrearkProcessor final : public juce::AudioProcessor
{
public:
    BrearkProcessor();
    ~BrearkProcessor() override;

    breark::Brain& getBrain() noexcept { return *brain; }
    AlertPresenter& getAlertPresenter() noexcept { return *alertPresenter; }
    bool isActiveInstance() const noexcept { return brain->isActive (this); }

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    // A 64-bit host gets a native 64-bit path instead of a float round trip.
    bool supportsDoublePrecisionProcessing() const override { return true; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Settings are global (Application Support), not stored per Live set.
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    template <typename SampleType>
    void process (juce::AudioBuffer<SampleType>& buffer) noexcept;

    juce::SharedResourcePointer<breark::Brain> brain;
    juce::SharedResourcePointer<AlertPresenter> alertPresenter; // toast + sound, one per process

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrearkProcessor)
};
