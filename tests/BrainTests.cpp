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
            Brain brain (tempStateFile());
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
            Brain brain (tempStateFile());
            int token = 0;
            start (brain, token, rate);

            for (int i = 0; i < 1000; ++i) // 10 s of audio, as fast as possible
                brain.push (makeBlock (i), false);

            const auto s = waitFor (brain, [] (const Snapshot& x) { return x.rendersSeen > 0; });

            expect (s.rendering, "burst not detected as export");
            expectEquals (s.rendersSeen, 1);
            brain.detach (&token);
        }

        beginTest ("State is saved when the Brain shuts down and restored on the next start");
        {
            const auto file = tempStateFile();
            float batteryBefore = 0.0f;

            {
                Brain brain (file);
                int token = 0;
                start (brain, token, rate);
                brain.setSettings ({ Setup::headphones, LevelPreset::loud });

                for (int i = 0; i < 250; ++i) // 2.5 s at a loud headphone level
                {
                    brain.push (makeBlock (i), false);
                    juce::Thread::sleep (10);
                }

                batteryBefore = waitFor (brain, [] (const Snapshot& x) { return x.battery < 100.0f; }).battery;
                brain.detach (&token);
            }

            expect (file.existsAsFile(), "no state file written");
            expect (batteryBefore < 100.0f, "battery did not drain");

            Brain restored (file);
            int token = 0;
            start (restored, token, rate);
            const auto s = waitFor (restored, [] (const Snapshot& x) { return x.battery < 100.0f; });

            expect (restored.getSettings().setup == Setup::headphones);
            expect (restored.getSettings().preset == LevelPreset::loud);
            expectWithinAbsoluteError (s.battery, batteryBefore, 0.5f);
            restored.detach (&token);
            file.deleteFile();
        }

        beginTest ("A host-flagged export is detected and never analysed");
        {
            Brain brain (tempStateFile());
            int token = 0;
            start (brain, token, rate);

            for (int i = 0; i < 30; ++i)
            {
                brain.push (makeBlock (i), true);
                juce::Thread::sleep (10);
            }

            const auto s = waitFor (brain, [] (const Snapshot& x) { return x.rendersSeen == 1 && ! x.rendering; }, 5000.0);

            expect (! s.rendering);
            expectEquals (s.rendersSeen, 1);
            expect (s.lastRenderHadHostFlag);
            expect (std::isinf (s.lufsShortTerm), "exported audio was analysed");
            brain.detach (&token);
        }

        beginTest ("Audio that stops arriving (device stopped or unplugged) is silence, not the last level");
        {
            Brain brain (tempStateFile());
            int token = 0;
            start (brain, token, rate);

            for (int i = 0; i < 200; ++i)
            {
                brain.push (makeBlock (i), false);
                juce::Thread::sleep (10);
            }

            expect (! brain.getSnapshot().silent);

            const auto stalled = waitFor (brain, [] (const Snapshot& x) { return x.audioStalled; });
            expect (stalled.audioStalled, "stall not detected");
            expect (stalled.silent && std::isinf (stalled.lufsShortTerm), "the last level is still counted");

            juce::Thread::sleep (2500);
            const auto later = brain.getSnapshot();
            expectEquals (later.listeningSecondsToday, stalled.listeningSecondsToday, "listening time kept running");
            brain.detach (&token);
        }

        beginTest ("Switched off in Live: frozen, counted as OFF, and it carries on when switched on");
        {
            Brain brain (tempStateFile());
            int token = 0;
            start (brain, token, rate);
            brain.setSettings ({ Setup::headphones, LevelPreset::loud }); // drains visibly within seconds

            for (int i = 0; i < 200; ++i)
            {
                brain.push (makeBlock (i), false);
                juce::Thread::sleep (10);
            }

            auto bypassFor = [&brain] (int blocks)
            {
                for (int i = 0; i < blocks; ++i)
                {
                    brain.pushBypassed (blockSize);
                    juce::Thread::sleep (10);
                }
            };

            bypassFor (50);
            const auto off = waitFor (brain, [] (const Snapshot& x) { return x.hostOff; });
            expect (off.hostOff, "switch-off not detected");

            bypassFor (300); // 3 s
            const auto frozen = brain.getSnapshot();
            expect (frozen.hostOff);
            expectEquals (frozen.battery, off.battery, "the battery moved while switched off");
            expectEquals (frozen.listeningSecondsToday, off.listeningSecondsToday);
            expect (! frozen.recovering);
            expect (frozen.offSecondsToday >= 2.0, "off time not counted: " + juce::String (frozen.offSecondsToday));

            const auto history = brain.getHistory();
            expect (! history.today.empty() && history.today.back().off, "today's curve is not marked OFF");
            expectEquals (history.lastDays.back().offSeconds, frozen.offSecondsToday);

            for (int i = 0; i < 200; ++i)
            {
                brain.push (makeBlock (i), false);
                juce::Thread::sleep (10);
            }

            const auto on = waitFor (brain, [&] (const Snapshot& x) { return ! x.hostOff && x.battery < frozen.battery; });
            expect (! on.hostOff && on.battery < frozen.battery, "did not carry on after switching on");
            brain.detach (&token);
        }

        beginTest ("Several instances: the first listens, the next takes over when it is removed");
        {
            Brain brain (tempStateFile());
            int first = 0, second = 0, third = 0;
            brain.attach (&first);
            brain.attach (&second);
            brain.attach (&third);

            expect (brain.isActive (&first) && ! brain.isActive (&second) && ! brain.isActive (&third));

            brain.detach (&second); // an inactive one leaves: nothing changes
            expect (brain.isActive (&first));

            brain.detach (&first);
            expect (brain.isActive (&third), "the next instance did not take over");

            brain.detach (&third);
            expect (! brain.isActive (&third));
        }
    }

private:
    // Polls instead of sleeping a fixed time: macOS coalesces background-thread
    // wakes (measured 150-250 ms), so fixed sleeps make tests flaky.
    template <typename Predicate>
    static Snapshot waitFor (const Brain& brain, Predicate&& done, double timeoutMs = 3000.0)
    {
        const auto startMs = juce::Time::getMillisecondCounterHiRes();
        auto s = brain.getSnapshot();

        while (! done (s) && juce::Time::getMillisecondCounterHiRes() - startMs < timeoutMs)
        {
            juce::Thread::sleep (10);
            s = brain.getSnapshot();
        }

        return s;
    }

    // Tests must never touch the real listening history.
    static juce::File tempStateFile()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("brEARk-brain-test", ".json");
    }

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

