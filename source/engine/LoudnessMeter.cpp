#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace breark
{
namespace
{
constexpr auto minusInfinity = -std::numeric_limits<float>::infinity();
constexpr double pi = 3.14159265358979323846;
} // namespace

//==============================================================================
// Analog prototypes of the BS.1770 filters, re-derived for any sample rate
// (same approach as libebur128). At 48 kHz they reproduce the published table.
BiquadCoefficients LoudnessMeter::makeShelfStage (double sampleRate) noexcept
{
    constexpr double f0 = 1681.974450955533;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q = 0.7071752369554196;

    const auto k = std::tan (pi * f0 / sampleRate);
    const auto vh = std::pow (10.0, gainDb / 20.0);
    const auto vb = std::pow (vh, 0.4996667741545416);
    const auto a0 = 1.0 + k / q + k * k;

    return { (vh + vb * k / q + k * k) / a0,
             2.0 * (k * k - vh) / a0,
             (vh - vb * k / q + k * k) / a0,
             2.0 * (k * k - 1.0) / a0,
             (1.0 - k / q + k * k) / a0 };
}

BiquadCoefficients LoudnessMeter::makeHighPassStage (double sampleRate) noexcept
{
    constexpr double f0 = 38.13547087602444;
    constexpr double q = 0.5003270373238773;

    const auto k = std::tan (pi * f0 / sampleRate);
    const auto a0 = 1.0 + k / q + k * k;

    return { 1.0, -2.0, 1.0,
             2.0 * (k * k - 1.0) / a0,
             (1.0 - k / q + k * k) / a0 };
}

//==============================================================================
void LoudnessMeter::prepare (double sampleRate)
{
    for (auto& stage : shelf)
        stage.c = makeShelfStage (sampleRate);

    for (auto& stage : highPass)
        stage.c = makeHighPassStage (sampleRate);

    subBlockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto* stages : { &shelf, &highPass })
        for (auto& stage : *stages)
            stage.z1 = stage.z2 = 0.0;

    subBlockFill = 0;
    subBlockEnergy = 0.0;
    subBlockPeak = 0.0f;
    energies.fill (0.0);
    peaks.fill (0.0f);
    writeIndex = 0;
    numFilled = 0;
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = highPass[0].process (shelf[0].process (left[i]));
        const auto r = highPass[1].process (shelf[1].process (right[i]));

        subBlockEnergy += l * l + r * r;
        subBlockPeak = std::max ({ subBlockPeak, std::abs (left[i]), std::abs (right[i]) });

        if (++subBlockFill == subBlockLength)
        {
            energies[(size_t) writeIndex] = subBlockEnergy / subBlockLength;
            peaks[(size_t) writeIndex] = subBlockPeak;
            writeIndex = (writeIndex + 1) % subBlocksShortTerm;
            numFilled = std::min (numFilled + 1, subBlocksShortTerm);

            subBlockFill = 0;
            subBlockEnergy = 0.0;
            subBlockPeak = 0.0f;
        }
    }
}

//==============================================================================
float LoudnessMeter::meanLufs (int numSubBlocks) const noexcept
{
    const auto n = std::min (numSubBlocks, numFilled);

    if (n == 0)
        return minusInfinity;

    double sum = 0.0;

    for (int k = 1; k <= n; ++k)
        sum += energies[(size_t) ((writeIndex - k + subBlocksShortTerm) % subBlocksShortTerm)];

    // BS.1770: −0.691 + 10·log10 (Σ G·z), with G = 1 for left and right.
    return sum > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (sum / n)) : minusInfinity;
}

float LoudnessMeter::getMomentaryLufs() const noexcept { return meanLufs (subBlocksMomentary); }
float LoudnessMeter::getShortTermLufs() const noexcept { return meanLufs (subBlocksShortTerm); }

float LoudnessMeter::getShortTermPeakDb() const noexcept
{
    const auto peak = *std::max_element (peaks.begin(), peaks.end());
    return peak > 0.0f ? 20.0f * std::log10 (peak) : minusInfinity;
}

} // namespace breark
