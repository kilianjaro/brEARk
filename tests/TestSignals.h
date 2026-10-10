#pragma once

#include <cmath>
#include <vector>

namespace breark::test
{
inline std::vector<float> sine (double frequency, double amplitude, double sampleRate, double seconds)
{
    std::vector<float> samples ((size_t) (sampleRate * seconds));

    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = (float) (amplitude * std::sin (2.0 * 3.14159265358979323846 * frequency * (double) i / sampleRate));

    return samples;
}

inline double dbToGain (double db)
{
    return std::pow (10.0, db / 20.0);
}
} // namespace breark::test
