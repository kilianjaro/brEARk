#include "engine/LoudnessMeter.h"
#include "RealisticMusic.h"
#include "engine/GridLoopDetector.h"
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

        // Noise with a random level every 250 ms: never repeats, but "moves" like
        // music, so the loop detector does its full search (its worst case).
        auto gain = 1.0f;

        for (size_t i = 0; i < left.size(); ++i)
        {
            if (i % (size_t) (rate / 4) == 0)
                gain = 0.05f + rng.nextFloat();

            left[i] = gain * (rng.nextFloat() * 2.0f - 1.0f);
            right[i] = gain * (rng.nextFloat() * 2.0f - 1.0f);
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

        // Loop detection, fast tempo (most 16ths per second), stamped 512-sample blocks.
        test::HouseOptions house;
        house.bpm = 175.0;
        house.sampleRate = rate;
        juce::Random human (3);
        const auto music = test::renderHouse ((int) (seconds * 175.0 / 240.0), [] (int bar) { return bar % 3; }, house, human);

        GridLoopDetector loops;
        loops.prepare (rate);
        const auto samplesPerQuarter = 60.0 * rate / house.bpm;
        const auto start = juce::Time::getHighResolutionTicks();

        for (juce::int64 pos = 0; pos + 512 <= (juce::int64) music.size(); pos += 512)
        {
            BlockStamp stamp;
            stamp.streamPosition = pos;
            stamp.ppq = (double) pos / samplesPerQuarter;
            stamp.bpm = house.bpm;
            stamp.playing = true;
            loops.addStamp (stamp);
            loops.addAudio (music.data() + pos, music.data() + pos, 512, pos);

            if ((pos / 512) % (juce::int64) (rate / 512) == 0)
                loops.update (1.0, false);
        }

        const auto musicSeconds = (double) music.size() / rate;
        const auto repetitionPercent = 100.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) / musicSeconds;

        std::cout << "    loudness (K-filter)  " << loudnessPercent << " % of one core\n"
                  << "    spectrum (FFT)       " << spectrumPercent << " % of one core\n"
                  << "    loop detection       " << repetitionPercent << " % of one core\n";

        expect (loudnessPercent + spectrumPercent + repetitionPercent < 0.5, "analysis exceeds its CPU budget");
    }
};

static PerformanceTests performanceTests;

} // namespace breark
