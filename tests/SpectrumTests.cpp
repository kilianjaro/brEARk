#include "TestSignals.h"
#include "engine/SpectrumAnalyser.h"

#include <juce_core/juce_core.h>

namespace breark
{
class SpectrumTests final : public juce::UnitTest
{
public:
    SpectrumTests() : juce::UnitTest ("Spectrum (A-weighting, bands)", "brEARk") {}

    void runTest() override
    {
        beginTest ("A-weighting curve matches IEC 61672");
        {
            auto aDb = [] (double f) { return 10.0 * std::log10 (SpectrumAnalyser::aWeightingPowerGain (f)); };
            expectWithinAbsoluteError (aDb (1000.0), 0.0, 0.01);
            expectWithinAbsoluteError (aDb (100.0), -19.1, 0.1);
            expectWithinAbsoluteError (aDb (2000.0), 1.2, 0.1);
            expectWithinAbsoluteError (aDb (10000.0), -2.5, 0.1);
        }

        for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            const auto suffix = " @ " + juce::String (rate, 0) + " Hz";

            beginTest ("Level: full-scale 1 kHz sine is -3.01 dB(A)" + suffix);
            {
                const auto totals = analyse (1000.0, rate);
                expect (totals.frames > 0);
                expectWithinAbsoluteError (toDb (totals.aWeighted / totals.frames), -3.01, 0.2);
            }

            beginTest ("Level: 100 Hz is A-weighted by -19.1 dB" + suffix);
            {
                // Looser: the FFT's main lobe spans ±2 bins of the steep A-curve here.
                const auto totals = analyse (100.0, rate);
                expectWithinAbsoluteError (toDb (totals.aWeighted / totals.frames), -3.01 - 19.1, 0.5);
            }

            beginTest ("Bands: 3 kHz is all presence, 500 Hz none" + suffix);
            {
                const auto inside = analyse (3000.0, rate);
                expectWithinAbsoluteError (toDb (inside.presence / inside.flat), 0.0, 0.2);

                const auto outside = analyse (500.0, rate);
                expect (toDb (outside.presence / outside.flat) < -40.0);
            }

            beginTest ("Bands: 10 kHz is all highs" + suffix);
            {
                const auto totals = analyse (10000.0, rate);
                expectWithinAbsoluteError (toDb (totals.highs / totals.flat), 0.0, 0.2);
            }
        }
    }

private:
    static SpectrumAnalyser::Totals analyse (double frequency, double rate)
    {
        SpectrumAnalyser analyser;
        analyser.prepare (rate);

        // Mono programme on the left channel, right silent.
        const auto left = test::sine (frequency, 1.0, rate, 2.0);
        const std::vector<float> right (left.size(), 0.0f);
        analyser.process (left.data(), right.data(), (int) left.size());

        return analyser.takeTotals();
    }

    static double toDb (double power) { return 10.0 * std::log10 (power); }
};

static SpectrumTests spectrumTests;

} // namespace breark
