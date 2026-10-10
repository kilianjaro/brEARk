// brEARk null test
// ----------------
// Loads the installed brEARk VST3 and AU the way a host does, streams noise
// through them in irregular block sizes at several sample rates, layouts and
// precisions, and requires the output to be bit-identical to the input.
//
// Usage: brEARk_NullTest [path/to/brEARk.vst3]
// Exit code 0 = every pass is bit-exact.

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <cstring>
#include <iostream>

namespace
{
constexpr int maxBlockSize   = 2048;
constexpr int blockPattern[] = { 1, 7, 64, 128, 333, 512, 2048, 31, 256, 1024, 4, 1500 };

struct Totals
{
    int passes   = 0;
    int failures = 0;
};

template <typename SampleType>
void fillTestSignal (juce::AudioBuffer<SampleType>& buffer, juce::Random& rng)
{
    constexpr auto int64Max = (double) std::numeric_limits<juce::int64>::max();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        // Full-mantissa noise up to ±2.0 (a float mix bus can exceed 0 dBFS),
        // so any rounding or precision loss anywhere changes bits.
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = (SampleType) (2.0 * (double) rng.nextInt64() / int64Max);

        // Edge cases: negative zero, denormals, smallest normal, full scale.
        data[0] = (SampleType) -0.0;
        data[1] = std::numeric_limits<SampleType>::denorm_min();
        data[2] = -std::numeric_limits<SampleType>::denorm_min();
        data[3] = std::numeric_limits<SampleType>::min();
        data[4] = (SampleType) 1.0;
        data[5] = (SampleType) -1.0;
    }
}

template <typename SampleType>
bool runPass (juce::AudioPluginInstance& plugin, double sampleRate,
              const juce::AudioChannelSet& channels, juce::String& error)
{
    plugin.releaseResources();

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (channels);
    layout.outputBuses.add (channels);

    if (! plugin.setBusesLayout (layout))
    {
        error = "layout rejected";
        return false;
    }

    plugin.setProcessingPrecision (std::is_same_v<SampleType, double> ? juce::AudioProcessor::doublePrecision
                                                                       : juce::AudioProcessor::singlePrecision);
    plugin.prepareToPlay (sampleRate, maxBlockSize);

    if (plugin.getLatencySamples() != 0)
    {
        error = "reports " + juce::String (plugin.getLatencySamples()) + " samples latency";
        return false;
    }

    const auto numChannels  = channels.size();
    const auto totalSamples = (int) sampleRate; // one second of audio

    juce::AudioBuffer<SampleType> input (numChannels, totalSamples);
    juce::AudioBuffer<SampleType> block (numChannels, maxBlockSize);
    juce::Random rng (0x6272454172); // fixed seed: reproducible runs
    fillTestSignal (input, rng);

    juce::MidiBuffer midi;
    auto ok = true;

    for (int pos = 0, step = 0; pos < totalSamples && ok; ++step)
    {
        const auto n = juce::jmin (blockPattern[(size_t) step % std::size (blockPattern)], totalSamples - pos);
        block.setSize (numChannels, n, false, false, true);

        for (int ch = 0; ch < numChannels; ++ch)
            block.copyFrom (ch, 0, input, ch, pos, n);

        plugin.processBlock (block, midi);

        for (int ch = 0; ch < numChannels && ok; ++ch)
        {
            if (std::memcmp (block.getReadPointer (ch), input.getReadPointer (ch, pos), (size_t) n * sizeof (SampleType)) != 0)
            {
                error = "output differs from input in samples " + juce::String (pos) + "-" + juce::String (pos + n)
                      + ", channel " + juce::String (ch);
                ok = false;
            }
        }

        pos += n;
    }

    plugin.releaseResources();
    return ok;
}

template <typename SampleType>
void runAndReport (juce::AudioPluginInstance& plugin, const juce::String& format, double sampleRate,
                   const juce::AudioChannelSet& channels, Totals& totals)
{
    juce::String error;
    const auto ok = runPass<SampleType> (plugin, sampleRate, channels, error);

    std::cout << (ok ? "  PASS  " : "  FAIL  ")
              << format.paddedRight (' ', 10)
              << channels.getDescription().paddedRight (' ', 8)
              << juce::String (sampleRate, 0).paddedLeft (' ', 6) << " Hz  "
              << (std::is_same_v<SampleType, double> ? "64-bit" : "32-bit")
              << (ok ? "" : "   " + error) << "\n";

    ok ? ++totals.passes : ++totals.failures;
}

void testPlugin (const juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description, Totals& totals)
{
    std::cout << "\n" << description.pluginFormatName << ": " << description.fileOrIdentifier << "\n";

    juce::String error;
    auto plugin = formats.createPluginInstance (description, 48000.0, maxBlockSize, error);

    if (plugin == nullptr)
    {
        std::cout << "  FAIL  could not load: " << error << "\n";
        ++totals.failures;
        return;
    }

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const juce::AudioChannelSet layouts[] = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() };

    for (const auto& channels : layouts)
    {
        for (auto rate : rates)
        {
            runAndReport<float> (*plugin, description.pluginFormatName, rate, channels, totals);

            if (plugin->supportsDoublePrecisionProcessing())
                runAndReport<double> (*plugin, description.pluginFormatName, rate, channels, totals);
        }
    }
}
} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser; // plugin hosting needs a message manager

    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager (formats);

    const auto vst3Path = argc > 1 ? juce::File (juce::String (argv[1]))
                                   : juce::File ("~/Library/Audio/Plug-Ins/VST3/brEARk.vst3");

    std::cout << "brEARk null test: output must equal input, bit for bit.\n";

    Totals totals;

    for (auto* format : formats.getFormats())
    {
        juce::OwnedArray<juce::PluginDescription> found;

        if (format->getName() == "VST3")
            format->findAllTypesForFile (found, vst3Path.getFullPathName());

        if (format->getName() == "AudioUnit")
            for (const auto& identifier : format->searchPathsForPlugins ({}, false, false))
                if (identifier.containsIgnoreCase ("Brek,Totm"))
                    format->findAllTypesForFile (found, identifier);

        for (auto* description : found)
            testPlugin (formats, *description, totals);
    }

    std::cout << "\n" << totals.passes << " passed, " << totals.failures << " failed\n";

    if (totals.passes == 0)
        std::cout << "No brEARk plugin found. Build it first (it is copied to ~/Library/Audio/Plug-Ins).\n";

    return (totals.failures == 0 && totals.passes > 0) ? 0 : 1;
}
