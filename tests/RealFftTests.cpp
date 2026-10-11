#include "engine/Brain.h"
#include "engine/RealFft.h"

#include <juce_dsp/juce_dsp.h>

#include <malloc/malloc.h>

#include <iostream>
#include <map>

namespace breark
{
namespace
{
double allocatedMB()
{
    malloc_statistics_t stats {};
    malloc_zone_statistics (nullptr, &stats);
    return (double) stats.size_in_use / 1048576.0;
}
} // namespace

class RealFftTests final : public juce::UnitTest
{
public:
    RealFftTests() : juce::UnitTest ("FFT and engine memory", "brEARk") {}

    void runTest() override
    {
        beginTest ("RealFft gives JUCE's real FFT result (bins 0 … N/2, same scale), with a radix-2 setup");
        {
            juce::Random random (11);

            for (const auto order : { 11, 12, 13 })
            {
                const auto n = 1 << order;
                std::vector<float> ours ((size_t) n * 2, 0.0f), juces ((size_t) n * 2, 0.0f);

                for (int i = 0; i < n; ++i)
                    ours[(size_t) i] = juces[(size_t) i] = 0.5f * (random.nextFloat() * 2.0f - 1.0f)
                                                         + 0.3f * (float) std::sin (0.05 * i);

                RealFft fft (order);
                juce::dsp::FFT reference (order);
                fft.performRealOnlyForwardTransform (ours.data());
                reference.performRealOnlyForwardTransform (juces.data(), true);

                auto worst = 0.0f, largest = 0.0f;

                for (int k = 0; k <= n + 1; ++k)
                {
                    worst = std::max (worst, std::abs (ours[(size_t) k] - juces[(size_t) k]));
                    largest = std::max (largest, std::abs (juces[(size_t) k]));
                }

                expect (worst <= 1.0e-6f * largest, "order " + juce::String (order) + ": differs by " + juce::String (worst)
                                                    + " of " + juce::String (largest));
            }
        }

        beginTest ("RealFft setups are small (JUCE's radix-5 setup: 0.86 MB at 2048, 3.2 MB at 8192)");
        {
            for (const auto order : { 11, 13 })
            {
                const auto before = allocatedMB();
                RealFft fft (order);
                const auto used = allocatedMB() - before;
                expect (used < (order == 11 ? 0.25 : 0.8), "order " + juce::String (order) + ": " + juce::String (used, 3) + " MB");
            }
        }

        beginTest ("Engine memory: under 6 MB at every sample rate, and rate changes don't pile up");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("brEARk-memory-test", ".json");
            const auto before = allocatedMB();
            std::map<double, std::vector<double>> used;

            {
                Brain brain (file);
                int token = 0;
                brain.attach (&token);

                for (const auto rate : { 48000.0, 96000.0, 192000.0, 44100.0, 48000.0, 96000.0, 192000.0, 44100.0 })
                {
                    brain.prepare (rate);

                    for (int wait = 0; wait < 1000 && ! juce::exactlyEqual (brain.getSnapshot().sampleRate, rate); ++wait)
                        juce::Thread::sleep (5);

                    const auto block = (int) (rate / 100.0);
                    juce::AudioBuffer<float> buffer (2, block);
                    juce::Random random (1);

                    for (int i = 0; i < 100; ++i) // 1 s of noise, at the pace of an audio callback
                    {
                        for (int ch = 0; ch < 2; ++ch)
                            for (int s = 0; s < block; ++s)
                                buffer.setSample (ch, s, 0.2f * (random.nextFloat() * 2.0f - 1.0f));

                        brain.push (buffer, false);
                        juce::Thread::sleep (10);
                    }

                    juce::Thread::sleep (300);
                    used[rate].push_back (allocatedMB() - before);
                }

                brain.detach (&token);
            }

            file.deleteFile();

            for (const auto& [rate, values] : used)
            {
                std::cout << "    " << juce::String (rate, 0) << " Hz: " << juce::String (values[0], 2) << " MB, again "
                          << juce::String (values[1], 2) << " MB\n";

                expect (values[0] < 6.0 && values[1] < 6.0, juce::String (rate, 0) + " Hz uses " + juce::String (values[1], 2) + " MB");
                expect (values[1] - values[0] < 0.1, juce::String (rate, 0) + " Hz grew by " + juce::String (values[1] - values[0], 2) + " MB");
            }
        }
    }
};

static RealFftTests realFftTests;

} // namespace breark
