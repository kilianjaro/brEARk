#include "dsp/PowerDown.h"
#include "engine/ModelConstants.h"

#include <juce_core/juce_core.h>

#include <cstring>

namespace breark
{
namespace
{
constexpr double rate = 44100.0;
constexpr int block = 512;

// A continuous stereo 1 kHz sine at −6 dBFS, block by block.
struct SineSource
{
    juce::int64 position = 0;

    juce::AudioBuffer<float> next (bool audible = true)
    {
        juce::AudioBuffer<float> buffer (2, block);

        for (int i = 0; i < block; ++i, ++position)
        {
            const auto v = audible ? (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) position / rate)) : 0.0f;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        return buffer;
    }
};

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;

    return true;
}

// Largest jump between neighbouring samples: a click shows up as a spike.
struct Steps
{
    float last = 0.0f;
    bool first = true;
    float maxStep = 0.0f;
    float maxLevel = 0.0f;

    void add (const juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto v = buffer.getSample (0, i);

            if (! first)
                maxStep = std::max (maxStep, std::abs (v - last));

            maxLevel = std::max (maxLevel, std::abs (v));
            last = v;
            first = false;
        }
    }
};

constexpr float sineMaxStep = (float) (0.5 * juce::MathConstants<double>::twoPi * 1000.0 / rate); // ≈ 0.071
} // namespace

//==============================================================================
class PowerDownTests final : public juce::UnitTest
{
public:
    PowerDownTests() : juce::UnitTest ("Power-down (vinyl spin-down / spin-up)", "brEARk") {}

    void runTest() override
    {
        const auto downBlocks = (int) std::ceil (model::spinDownSeconds * rate / block) + 1;
        const auto upBlocks = (int) std::ceil ((model::spinUpSeconds + 0.01) * rate / block) + 1;

        beginTest ("Not unplugged: bit-exact, with and without a ring buffer, 32 and 64 bit");
        {
            PowerDown power;
            power.prepare (rate);
            SineSource sine;
            auto allIdentical = true;

            for (int b = 0; b < 50; ++b)
            {
                if (b == 10)
                    power.manageBuffer (true, rate); // Hard mode switched on mid-stream

                auto buffer = sine.next();
                const auto original = buffer;
                power.process (buffer, false, false);
                allIdentical = allIdentical && identical (buffer, original);
            }

            juce::AudioBuffer<double> wide (2, block);
            for (int i = 0; i < block; ++i)
                wide.setSample (0, i, 0.123456789012345 * i), wide.setSample (1, i, -0.5);

            const auto wideOriginal = wide;
            power.process (wide, false, false);

            expect (allIdentical, "audio changed while not unplugged");
            expect (std::memcmp (wide.getReadPointer (0), wideOriginal.getReadPointer (0), sizeof (double) * block) == 0);
        }

        beginTest ("Spin-down: falls to exact digital silence, no clicks, never louder");
        {
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;
            Steps steps;

            for (int b = 0; b < 5; ++b) // adopt the ring, steady state
            {
                auto buffer = sine.next();
                power.process (buffer, false, false);
                steps.add (buffer);
            }

            for (int b = 0; b < downBlocks; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, true, false);
                steps.add (buffer);
            }

            expect (power.getStage() == PowerDown::Stage::stopped);
            expect (steps.maxStep <= sineMaxStep * 1.05f, "click: step " + juce::String (steps.maxStep));
            expect (steps.maxLevel <= 0.5f + 1.0e-4f, "louder than the input");

            auto silent = true;

            for (int b = 0; b < 20; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, true, false);
                silent = silent && buffer.getMagnitude (0, block) == 0.0f && buffer.getMagnitude (1, 0, block) == 0.0f;
            }

            expect (silent, "stopped must be exact zeros");
        }

        beginTest ("Spin-up: smooth, then back to bit-exact pass-through");
        {
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;

            for (int b = 0; b < 2 + downBlocks; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, b >= 2, false);
            }

            Steps steps;

            for (int b = 0; b < upBlocks; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, false, false);
                steps.add (buffer);
            }

            expect (power.getStage() == PowerDown::Stage::run);
            // The brief over-speed raises the pitch (×1.35) and with it the step size.
            expect (steps.maxStep <= sineMaxStep * 1.45f, "click: step " + juce::String (steps.maxStep));
            expect (steps.maxLevel <= 0.5f + 1.0e-3f, "louder than the input");

            auto allIdentical = true;

            for (int b = 0; b < 20; ++b)
            {
                auto buffer = sine.next();
                const auto original = buffer;
                power.process (buffer, false, false);
                allIdentical = allIdentical && identical (buffer, original);
            }

            expect (allIdentical, "not bit-exact after the spin-up");
        }

        beginTest ("Break over while nothing plays: untouched, and the spin-up waits for the next sound");
        {
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;

            for (int b = 0; b < 2 + downBlocks; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, b >= 2, false);
            }

            auto quiet = sine.next (false);
            const auto quietOriginal = quiet;
            power.process (quiet, false, false);
            expect (identical (quiet, quietOriginal));
            expect (power.getStage() == PowerDown::Stage::run);

            auto sound = sine.next();
            const auto soundOriginal = sound;
            power.process (sound, false, false);
            expect (power.getStage() == PowerDown::Stage::spinningUp, "the next sound should spin up");
            expect (! identical (sound, soundOriginal));
        }

        beginTest ("Exports pass untouched, even while unplugged");
        {
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;

            for (int b = 0; b < 2 + downBlocks; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, b >= 2, false);
            }

            auto exported = sine.next();
            const auto original = exported;
            power.process (exported, true, true);
            expect (identical (exported, original), "an export was changed");

            auto after = sine.next();
            power.process (after, true, false);
            expectEquals (after.getMagnitude (0, block), 0.0f);
        }

        beginTest ("Without a ring buffer: the same curve as a plain fade, still click-free");
        {
            PowerDown power;
            power.prepare (rate);
            SineSource sine;
            Steps steps;

            for (int b = 0; b < downBlocks; ++b)
            {
                auto buffer = sine.next();
                power.process (buffer, true, false);
                steps.add (buffer);
            }

            expect (power.getStage() == PowerDown::Stage::stopped);
            expect (steps.maxStep <= sineMaxStep * 1.05f);
        }

        beginTest ("Sample rate raised while Hard mode is on: the old, too short ring is never read");
        {
            constexpr double highRate = 4.0 * rate;
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;

            auto buffer = sine.next();
            power.process (buffer, false, false); // adopts the 44.1 kHz ring (0.5 s too short at 176.4 kHz)

            power.prepare (highRate); // the device changed; the new ring arrives with the next timer tick
            const auto spinDownSamples = (int) (model::spinDownSeconds * highRate);
            auto plainFade = true;

            for (int done = 0; done < spinDownSamples; done += block)
            {
                buffer = sine.next();
                const auto original = buffer;
                power.process (buffer, true, false);

                // The plain fade only scales the live signal by 0…1; ring playback
                // would play earlier samples instead.
                for (int i = 0; i < block; ++i)
                {
                    const auto in = original.getSample (0, i);

                    if (std::abs (in) > 1.0e-3f)
                    {
                        const auto gain = buffer.getSample (0, i) / in;
                        plainFade = plainFade && gain >= -1.0e-6f && gain <= 1.0f + 1.0e-6f;
                    }
                }
            }

            expect (plainFade, "read from a ring sized for the old rate");
            expect (power.getStage() == PowerDown::Stage::stopped);

            power.manageBuffer (true, highRate); // the timer catches up: a matching ring
            buffer = sine.next();
            power.process (buffer, false, false); // stopped → spin-up armed, nothing breaks
        }

        beginTest ("Switched off in Live mid-ramp: jumps to the end state, then continues cleanly");
        {
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;

            auto buffer = sine.next();
            power.process (buffer, false, false);
            buffer = sine.next();
            power.process (buffer, true, false);
            expect (power.getStage() == PowerDown::Stage::spinningDown);

            power.skipRamp(); // the host bypasses brEARk
            expect (power.getStage() == PowerDown::Stage::stopped);

            buffer = sine.next();
            power.process (buffer, true, false); // switched on again, still unplugged
            expect (buffer.getMagnitude (0, block) == 0.0f, "still unplugged: exact zeros");
        }

        beginTest ("Hard mode off: the ring buffer is handed back and freed");
        {
            PowerDown power;
            power.prepare (rate);
            power.manageBuffer (true, rate);
            SineSource sine;

            auto buffer = sine.next();
            power.process (buffer, false, false); // adopts the ring

            power.manageBuffer (false, rate);
            buffer = sine.next();
            power.process (buffer, false, false); // hands it back

            power.manageBuffer (false, rate);     // frees it
            buffer = sine.next();
            const auto original = buffer;
            power.process (buffer, true, false);  // no ring now: falls back to the plain fade

            expect (power.getStage() == PowerDown::Stage::spinningDown);
            expect (! identical (buffer, original));
        }
    }
};

static PowerDownTests powerDownTests;

} // namespace breark
