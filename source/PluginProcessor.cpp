#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <utility>

//==============================================================================
BrearkProcessor::BrearkProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    brain->attach (this);
    startTimerHz (2);
}

BrearkProcessor::~BrearkProcessor()
{
    stopTimer();
    brain->detach (this);
}

void BrearkProcessor::timerCallback()
{
    const auto hardMode = brain->getSettings().strictness == breark::Strictness::hard;
    powerDown.manageBuffer (hardMode && isActiveInstance(), getSampleRate());
}

void BrearkProcessor::prepareToPlay (double sampleRate, int)
{
    // brEARk never delays audio, so Live's delay compensation never changes.
    setLatencySamples (0);
    brain->prepare (sampleRate);
    powerDown.prepare (sampleRate);
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
#if BREARK_DEV_METRICS
    const auto startTicks = juce::Time::getHighResolutionTicks();
#endif

    // The host's audio is left exactly as it arrived.
    // Only output channels without a matching input would be cleared; our
    // symmetric layouts never produce any, this guards against host oddities.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Analysis gets a read-only view: a copy goes into the lock-free FIFO.
    if (brain->isActive (this))
    {
        const auto exporting = isNonRealtime();
        breark::BlockStamp stamp; // no grid unless the host provides one

        if (auto* playHead = getPlayHead(); playHead != nullptr && ! exporting)
        {
            if (const auto position = playHead->getPosition())
            {
                brain->updateTransport (*position);

                const auto ppq = position->getPpqPosition();
                const auto bpm = position->getBpm();
                const auto signature = position->getTimeSignature();

                stamp.playing = position->getIsPlaying() && ppq.hasValue() && bpm.hasValue();
                stamp.ppq = ppq.orFallback (0.0);
                stamp.bpm = bpm.orFallback (0.0);
                stamp.barLengthQuarters = signature && signature->denominator > 0
                                            ? 4.0 * signature->numerator / signature->denominator : 4.0;
            }
        }

        brain->push (std::as_const (buffer), exporting, stamp);

        // The only code that may change the audio: Hard mode pulling the plug.
        // Untouched unless unplugged; exports always pass untouched.
        powerDown.process (buffer, brain->isUnplugRequested(), exporting || brain->isExportDetected());
    }

#if BREARK_DEV_METRICS
    brain->addAudioThreadTicks (juce::Time::getHighResolutionTicks() - startTicks);
#endif
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
