#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BrearkProcessor::BrearkProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void BrearkProcessor::prepareToPlay (double, int)
{
    // brEARk never delays audio, so Live's delay compensation never changes.
    setLatencySamples (0);
}

void BrearkProcessor::releaseResources() {}

bool BrearkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    // A pass-through needs identical input and output layouts: mono or stereo.
    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
template <typename SampleType>
void BrearkProcessor::process (juce::AudioBuffer<SampleType>& buffer) noexcept
{
    // The host's audio is left exactly as it arrived.
    // Only output channels without a matching input would be cleared; our
    // symmetric layouts never produce any, this guards against host oddities.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

void BrearkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer);
}

void BrearkProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    process (buffer);
}

//==============================================================================
juce::AudioProcessorEditor* BrearkProcessor::createEditor()
{
    return new BrearkEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrearkProcessor();
}
