#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

namespace breark
{
// Where a block sits on the host's musical grid, for tempo-synchronous analysis.
struct BlockStamp
{
    juce::int64 streamPosition = 0; // index of the block's first frame in the FIFO stream
    double ppq = 0.0;               // position in quarter notes at that frame
    double bpm = 0.0;
    double barLengthQuarters = 4.0;
    bool playing = false;           // false: no grid for this block
};

//==============================================================================
// Single-producer / single-consumer FIFO carrying a stereo float copy of the
// audio for analysis, plus one BlockStamp per block. The audio thread pushes,
// the worker thread pulls.
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
    bool push (const juce::AudioBuffer<SampleType>& buffer, BlockStamp stamp = {}) noexcept
    {
        const auto numSamples = buffer.getNumSamples();

        if (numSamples <= 0)
            return true;

        if (fifo.getFreeSpace() < numSamples)
        {
            droppedSamples.fetch_add (numSamples, std::memory_order_relaxed);
            return false;
        }

        // The stamp goes first, so the worker never sees audio without its stamp.
        // A full stamp FIFO only loses grid information, never audio.
        if (stampFifo.getFreeSpace() > 0)
        {
            stamp.streamPosition = framesWritten;
            int start1, size1, start2, size2;
            stampFifo.prepareToWrite (1, start1, size1, start2, size2);
            stamps[(size_t) (size1 > 0 ? start1 : start2)] = stamp;
            stampFifo.finishedWrite (1);
        }

        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
        copyIn (buffer, 0, start1, size1);
        copyIn (buffer, size1, start2, size2);
        fifo.finishedWrite (size1 + size2);

        framesWritten += numSamples;
        return true;
    }

    // Worker thread. Hands every ready frame to
    // consumer (left, right, numFrames, streamPosition) in up to two contiguous
    // slices, then frees them.
    template <typename Consumer>
    int pull (Consumer&& consumer)
    {
        const auto ready = fifo.getNumReady();

        if (ready <= 0)
            return 0;

        int start1, size1, start2, size2;
        fifo.prepareToRead (ready, start1, size1, start2, size2);

        if (size1 > 0)
            consumer (channels[0].data() + start1, channels[1].data() + start1, size1, framesRead);

        if (size2 > 0)
            consumer (channels[0].data() + start2, channels[1].data() + start2, size2, framesRead + size1);

        fifo.finishedRead (size1 + size2);
        framesRead += size1 + size2;
        return size1 + size2;
    }

    // Worker thread. Call before pull(): stamps always precede their audio.
    template <typename Consumer>
    void pullStamps (Consumer&& consumer)
    {
        const auto ready = stampFifo.getNumReady();
        int start1, size1, start2, size2;
        stampFifo.prepareToRead (ready, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            consumer (stamps[(size_t) (start1 + i)]);

        for (int i = 0; i < size2; ++i)
            consumer (stamps[(size_t) (start2 + i)]);

        stampFifo.finishedRead (size1 + size2);
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

    static constexpr int stampCapacity = 4096; // > 2 s of 32-sample blocks at 48 kHz
    juce::AbstractFifo stampFifo { stampCapacity + 1 };
    std::vector<BlockStamp> stamps = std::vector<BlockStamp> ((size_t) stampCapacity + 1);

    juce::int64 framesWritten = 0; // producer only
    juce::int64 framesRead = 0;    // consumer only
};

} // namespace breark
