#include "TestSignals.h"
#include "engine/Brain.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace breark
{
// End-to-end: audio thread → FIFO → worker → snapshot, in real time (~8 s).
class BrainTests final : public juce::UnitTest
{
public:
    BrainTests() : juce::UnitTest ("Brain (end-to-end, realtime)", "brEARk") {}

    void runTest() override
    {
        constexpr double rate = 48000.0;
        constexpr int blockSize = 480; // 10 ms
        const auto sine = test::sine (1000.0, test::dbToGain (-23.0), rate, 0.5);

        auto makeBlock = [&] (int index)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            const auto offset = (size_t) ((index * blockSize) % (int) (sine.size() - blockSize));
            block.copyFrom (0, 0, sine.data() + offset, blockSize);
            block.copyFrom (1, 0, sine.data() + offset, blockSize);
            return block;
        };

        beginTest ("Realtime playback is measured, and not mistaken for an export");
        {
            Brain brain;
            int token = 0;
            start (brain, token, rate);

            for (int i = 0; i < 400; ++i) // 4 s, paced like an audio callback
            {
                brain.push (makeBlock (i), false);
                juce::Thread::sleep (10);
            }

            juce::Thread::sleep (150);
            const auto s = brain.getSnapshot();

            expectWithinAbsoluteError (s.lufsShortTerm, -23.0f, 0.3f);
            expect (! s.silent);
            expect (! s.rendering, "realtime playback flagged as export");
            expectEquals (s.rendersSeen, 0);
            expectEquals ((int) s.samplesDropped, 0);
            std::cout << "    worker wake " << s.workerWakeMsAverage << " ms avg, " << s.workerWakeMsMax << " ms max, "
                      << s.workerLoadPercent << " % of one core\n";
            brain.detach (&token);
        }

        beginTest ("A faster-than-realtime burst (export without host flag) is detected");
        {
            Brain brain;
            int token = 0;
            start (brain, token, rate);

            for (int i = 0; i < 1000; ++i) // 10 s of audio, as fast as possible
                brain.push (makeBlock (i), false);

            juce::Thread::sleep (200);
            const auto s = brain.getSnapshot();

            expect (s.rendering, "burst not detected as export");
            expectEquals (s.rendersSeen, 1);
            brain.detach (&token);
        }

        beginTest ("A host-flagged export is detected and never analysed");
        {
            Brain brain;
            int token = 0;
            start (brain, token, rate);

            for (int i = 0; i < 30; ++i)
            {
                brain.push (makeBlock (i), true);
                juce::Thread::sleep (10);
            }

            juce::Thread::sleep (1800); // render hold time passes
            const auto s = brain.getSnapshot();

            expect (! s.rendering);
            expectEquals (s.rendersSeen, 1);
            expect (s.lastRenderHadHostFlag);
            expect (std::isinf (s.lufsShortTerm), "exported audio was analysed");
            brain.detach (&token);
        }
    }

private:
    static void start (Brain& brain, int& token, double rate)
    {
        brain.attach (&token);
        brain.prepare (rate);

        // Wait until the worker has picked up the configuration.
        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        while (brain.getSnapshot().sampleRate <= 0.0 && juce::Time::getMillisecondCounterHiRes() - startMs < 5000.0)
            juce::Thread::sleep (5);

        std::cout << "    worker configured after " << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - startMs) << " ms\n";
    }
};

static BrainTests brainTests;

} // namespace breark
