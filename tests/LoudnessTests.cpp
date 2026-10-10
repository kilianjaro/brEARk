#include "TestSignals.h"
#include "engine/LoudnessMeter.h"

#include <juce_core/juce_core.h>

namespace breark
{
class LoudnessTests final : public juce::UnitTest
{
public:
    LoudnessTests() : juce::UnitTest ("Loudness (ITU-R BS.1770)", "brEARk") {}

    void runTest() override
    {
        beginTest ("K-weighting matches the BS.1770 coefficient table at 48 kHz");
        {
            const auto shelf = LoudnessMeter::makeShelfStage (48000.0);
            expectWithinAbsoluteError (shelf.b0, 1.53512485958697, 1.0e-6);
            expectWithinAbsoluteError (shelf.b1, -2.69169618940638, 1.0e-6);
            expectWithinAbsoluteError (shelf.b2, 1.19839281085285, 1.0e-6);
            expectWithinAbsoluteError (shelf.a1, -1.69065929318241, 1.0e-6);
            expectWithinAbsoluteError (shelf.a2, 0.73248077421585, 1.0e-6);

            const auto highPass = LoudnessMeter::makeHighPassStage (48000.0);
            expectWithinAbsoluteError (highPass.b0, 1.0, 1.0e-12);
            expectWithinAbsoluteError (highPass.b1, -2.0, 1.0e-12);
            expectWithinAbsoluteError (highPass.b2, 1.0, 1.0e-12);
            expectWithinAbsoluteError (highPass.a1, -1.99004745483398, 1.0e-6);
            expectWithinAbsoluteError (highPass.a2, 0.99007225036621, 1.0e-6);
        }

        // EBU Tech 3341, cases 1 & 2: stereo 1 kHz sine at −23 / −33 dBFS
        // must read −23.0 / −33.0 LUFS (±0.1) momentary and short-term.
        for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            for (auto level : { -23.0, -33.0 })
            {
                beginTest ("EBU Tech 3341: " + juce::String (level, 0) + " dBFS sine @ " + juce::String (rate, 0) + " Hz");

                LoudnessMeter meter;
                meter.prepare (rate);

                const auto signal = test::sine (1000.0, test::dbToGain (level), rate, 5.0);
                meter.process (signal.data(), signal.data(), (int) signal.size());

                expectWithinAbsoluteError (meter.getMomentaryLufs(), (float) level, 0.1f);
                expectWithinAbsoluteError (meter.getShortTermLufs(), (float) level, 0.1f);
                expectWithinAbsoluteError (meter.getShortTermPeakDb(), (float) level, 0.05f);
            }
        }

        beginTest ("Silence reads -inf");
        {
            LoudnessMeter meter;
            meter.prepare (48000.0);

            const std::vector<float> silence (48000 * 4, 0.0f);
            meter.process (silence.data(), silence.data(), (int) silence.size());

            expect (std::isinf (meter.getShortTermLufs()) && meter.getShortTermLufs() < 0.0f);
            expect (std::isinf (meter.getShortTermPeakDb()) && meter.getShortTermPeakDb() < 0.0f);
        }
    }
};

static LoudnessTests loudnessTests;

} // namespace breark
