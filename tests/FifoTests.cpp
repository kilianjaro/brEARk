#include "engine/SampleFifo.h"

#include <juce_core/juce_core.h>

namespace breark
{
class FifoTests final : public juce::UnitTest
{
public:
    FifoTests() : juce::UnitTest ("Sample FIFO", "brEARk") {}

    void runTest() override
    {
        beginTest ("Stereo float blocks arrive intact and in order, across the wrap-around");
        {
            SampleFifo fifo (1000);
            std::vector<float> receivedLeft, receivedRight;
            float counter = 0.0f;

            for (int round = 0; round < 10; ++round)
            {
                juce::AudioBuffer<float> block (2, 300);

                for (int i = 0; i < 300; ++i, counter += 1.0f)
                {
                    block.setSample (0, i, counter);
                    block.setSample (1, i, -counter);
                }

                expect (fifo.push (block));
                collect (fifo, receivedLeft, receivedRight);
            }

            expectEquals ((int) receivedLeft.size(), 3000);

            for (size_t i = 0; i < receivedLeft.size(); ++i)
            {
                if (! juce::exactlyEqual (receivedLeft[i], (float) i) || ! juce::exactlyEqual (receivedRight[i], -(float) i))
                {
                    expect (false, "sample " + juce::String ((int) i) + " out of order");
                    break;
                }
            }

            expectEquals ((int) fifo.getDroppedSamples(), 0);
        }

        beginTest ("A block that doesn't fit is dropped whole and counted");
        {
            SampleFifo fifo (1000);
            juce::AudioBuffer<float> block (2, 600);
            block.clear();

            expect (fifo.push (block));
            expect (! fifo.push (block));
            expectEquals ((int) fifo.getDroppedSamples(), 600);
        }

        beginTest ("Each block's stamp carries its position in the stream");
        {
            SampleFifo fifo (1000);
            juce::AudioBuffer<float> block (2, 300);
            block.clear();

            for (int i = 0; i < 3; ++i)
            {
                BlockStamp stamp;
                stamp.ppq = i;
                expect (fifo.push (block, stamp));
            }

            std::vector<juce::int64> positions;
            fifo.pullStamps ([&] (const BlockStamp& s) { positions.push_back (s.streamPosition); });

            expectEquals ((int) positions.size(), 3);
            expectEquals ((int) positions[2], 600);

            juce::int64 firstAudioPosition = -1;
            fifo.pull ([&] (const float*, const float*, int, juce::int64 position)
            {
                if (firstAudioPosition < 0)
                    firstAudioPosition = position;
            });
            expectEquals ((int) firstAudioPosition, 0);
        }

        beginTest ("64-bit input is converted, mono input leaves the right channel silent");
        {
            SampleFifo fifo (100);
            juce::AudioBuffer<double> block (1, 4);

            for (int i = 0; i < 4; ++i)
                block.setSample (0, i, 0.25 * (i + 1));

            expect (fifo.push (block));

            std::vector<float> left, right;
            collect (fifo, left, right);

            expectEquals ((int) left.size(), 4);
            expectEquals (left[3], 1.0f);
            expectEquals (right[3], 0.0f);
        }
    }

private:
    static void collect (SampleFifo& fifo, std::vector<float>& left, std::vector<float>& right)
    {
        fifo.pull ([&] (const float* l, const float* r, int n, juce::int64)
        {
            left.insert (left.end(), l, l + n);
            right.insert (right.end(), r, r + n);
        });
    }
};

static FifoTests fifoTests;

} // namespace breark
