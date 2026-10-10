#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

namespace breark
{
//==============================================================================
// Single-producer / single-consumer FIFO carrying a stereo float copy of the
// audio for analysis. The audio thread pushes, the worker thread pulls.
//
// push() is wait-free and allocation-free. A block that doesn't fit is dropped
// whole and counted. It only ever reads the host's buffer.
class SampleFifo
{
public:
    explicit SampleFifo (int capacityInFrames)
        : fifo (capacityInFrames + 1) // AbstractFifo keeps one slot free
    {
        for (auto& channel : channels)
            channel.resize ((size_t) capacityInFrames + 1);
    }

    // Audio thread. Mono input is analysed as left-only, which BS.1770 treats
    // as a mono programme.
    template <typename SampleType>
    bool push (const juce::AudioBuffer<SampleType>& buffer) noexcept
    {
        const auto numSamples = buffer.getNumSamples();

        if (numSamples <= 0)
            return true;

        if (fifo.getFreeSpace() < numSamples)
        {
            droppedSamples.fetch_add (numSamples, std::memory_order_relaxed);
            return false;
        }

        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
        copyIn (buffer, 0, start1, size1);
        copyIn (buffer, size1, start2, size2);
        fifo.finishedWrite (size1 + size2);
        return true;
    }

    // Worker thread. Hands every ready frame to consumer (left, right, numFrames)
    // in up to two contiguous slices, then frees them.
    template <typename Consumer>
    int pull (Consumer&& consumer)
    {
        const auto ready = fifo.getNumReady();

        if (ready <= 0)
            return 0;

        int start1, size1, start2, size2;
        fifo.prepareToRead (ready, start1, size1, start2, size2);

        if (size1 > 0)
            consumer (channels[0].data() + start1, channels[1].data() + start1, size1);

        if (size2 > 0)
            consumer (channels[0].data() + start2, channels[1].data() + start2, size2);

        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    juce::int64 getDroppedSamples() const noexcept { return droppedSamples.load (std::memory_order_relaxed); }

private:
    template <typename SampleType>
    void copyIn (const juce::AudioBuffer<SampleType>& buffer, int sourceStart, int destStart, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dest = channels[(size_t) ch].data() + destStart;

            if (ch >= buffer.getNumChannels())
            {
                juce::FloatVectorOperations::clear (dest, numSamples);
                continue;
            }

            const auto* source = buffer.getReadPointer (ch, sourceStart);

            if constexpr (std::is_same_v<SampleType, float>)
                juce::FloatVectorOperations::copy (dest, source, numSamples);
            else
                for (int i = 0; i < numSamples; ++i)
                    dest[i] = (float) source[i];
        }
    }

    juce::AbstractFifo fifo;
    std::array<std::vector<float>, 2> channels;
    std::atomic<juce::int64> droppedSamples { 0 };
};

} // namespace breark
