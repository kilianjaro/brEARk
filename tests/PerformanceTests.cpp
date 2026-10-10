#include "engine/LoudnessMeter.h"
#include "engine/SpectrumAnalyser.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace breark
{
// Cost per second of 48 kHz stereo audio, per analysis stage. Printed so
// regressions are visible; the bound is generous to avoid flaky failures.
class PerformanceTests final : public juce::UnitTest
{
public:
    PerformanceTests() : juce::UnitTest ("Performance budget", "brEARk") {}

    void runTest() override
    {
        beginTest ("Analysis costs a fraction of a percent of one core");

        constexpr double rate = 48000.0;
        constexpr int seconds = 20;
        constexpr int chunk = 7200; // ~150 ms, like one worker wake

        juce::Random rng (42);
        std::vector<float> left ((size_t) rate * seconds), right (left.size());

        for (size_t i = 0; i < left.size(); ++i)
        {
            left[i] = rng.nextFloat() * 2.0f - 1.0f;
            right[i] = rng.nextFloat() * 2.0f - 1.0f;
        }

        LoudnessMeter loudness;
        loudness.prepare (rate);
        SpectrumAnalyser spectrum;
        spectrum.prepare (rate);

        auto timeStage = [&] (auto&& stage)
        {
            const auto start = juce::Time::getHighResolutionTicks();

            for (size_t pos = 0; pos + chunk <= left.size(); pos += chunk)
                stage (left.data() + pos, right.data() + pos, chunk);

            const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            return 100.0 * elapsed / seconds; // % of one core
        };

        const auto loudnessPercent = timeStage ([&] (const float* l, const float* r, int n) { loudness.process (l, r, n); });
        const auto spectrumPercent = timeStage ([&] (const float* l, const float* r, int n) { spectrum.process (l, r, n); });

        std::cout << "    loudness (K-filter)  " << loudnessPercent << " % of one core\n"
                  << "    spectrum (FFT)       " << spectrumPercent << " % of one core\n";

        expect (loudnessPercent + spectrumPercent < 0.5, "analysis exceeds its CPU budget");
    }
};

static PerformanceTests performanceTests;

} // namespace breark
