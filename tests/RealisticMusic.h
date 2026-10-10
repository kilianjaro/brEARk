#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <vector>

namespace breark::test
{
//==============================================================================
// A house-style arrangement, rendered bar by bar: four-on-the-floor kick,
// claps on 2 and 4, offbeat hats, a sustained pad chord and a sidechained
// 8th-note bass per bar. chordForBar decides the harmony of each bar (same
// chords = a loop). Humanisation: hat velocity ±hatJitterDb at random, and a
// free-running pad LFO (not synced to the loop) of ±padLfoDb.
struct HouseOptions
{
    double bpm = 124.0;
    double sampleRate = 44100.0;
    double hatJitterDb = 0.0;
    double padLfoDb = 0.0;
    double padLfoHz = 0.23;
    double padGainDb = 0.0; // an "EQ tweak" on the pad
};

inline std::vector<float> renderHouse (int numBars, const std::function<int (int bar)>& chordForBar,
                                       const HouseOptions& options, juce::Random& human)
{
    const auto sr = options.sampleRate;
    const auto beat = 60.0 / options.bpm;
    const auto barSamples = (size_t) std::llround (4.0 * beat * sr);
    std::vector<float> out (barSamples * (size_t) numBars, 0.0f);

    const double roots[] = { 110.0, 130.81, 146.83, 164.81, 174.61, 196.0 }; // A C D E F G
    constexpr auto twoPi = 6.283185307179586;

    auto addBurst = [&] (size_t start, double seconds, const std::function<double (double)>& shape)
    {
        for (size_t i = 0; i < (size_t) (seconds * sr) && start + i < out.size(); ++i)
            out[start + i] += (float) shape ((double) i / sr);
    };

    for (int bar = 0; bar < numBars; ++bar)
    {
        const auto barStart = (size_t) bar * barSamples;
        const auto root = roots[(size_t) chordForBar (bar) % 6];

        for (int b = 0; b < 4; ++b)
        {
            const auto beatStart = barStart + (size_t) std::llround (b * beat * sr);

            addBurst (beatStart, 0.25, [] (double t) { return 0.8 * std::sin (twoPi * 55.0 * t) * std::exp (-t / 0.09); });

            if (b == 1 || b == 3)
            {
                juce::Random clap (1000 + b); // a sample: the same noise every time
                addBurst (beatStart, 0.12, [&] (double t) { return 0.35 * (clap.nextDouble() * 2.0 - 1.0) * std::exp (-t / 0.04); });
            }

            const auto hatGain = std::pow (10.0, (human.nextDouble() * 2.0 - 1.0) * options.hatJitterDb / 20.0);
            juce::Random hat (2000);
            double previous = 0.0;
            addBurst (beatStart + (size_t) std::llround (0.5 * beat * sr), 0.04, [&] (double t)
            {
                const auto noise = hat.nextDouble() * 2.0 - 1.0;
                const auto highPassed = noise - previous; // crude high-pass: first difference
                previous = noise;
                return 0.25 * hatGain * highPassed * std::exp (-t / 0.012);
            });
        }

        for (size_t i = 0; i < barSamples; ++i)
        {
            const auto t = (double) i / sr;
            const auto absolute = (double) (barStart + i) / sr;
            const auto inBeat = std::fmod (t, beat);
            const auto sidechain = std::min (1.0, inBeat / 0.15);
            const auto lfo = std::pow (10.0, options.padLfoDb * std::sin (twoPi * options.padLfoHz * absolute) / 20.0);

            const auto pad = 0.06 * std::pow (10.0, options.padGainDb / 20.0) * lfo * (std::sin (twoPi * root * 2.0 * absolute)
                                           + std::sin (twoPi * root * 2.52 * absolute)
                                           + std::sin (twoPi * root * 3.0 * absolute));

            const auto bassPhase = std::fmod (t, beat / 2.0);
            const auto bass = 0.3 * sidechain * std::exp (-bassPhase / 0.2)
                            * (std::sin (twoPi * root / 2.0 * absolute) + 0.4 * std::sin (twoPi * root * absolute));

            out[barStart + i] += (float) (pad + bass);
        }
    }

    return out;
}
} // namespace breark::test
