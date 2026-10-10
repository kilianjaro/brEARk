#pragma once

#include <array>

namespace breark
{
struct BiquadCoefficients
{
    double b0, b1, b2, a1, a2;
};

//==============================================================================
// ITU-R BS.1770 loudness: K-weighting, ungated momentary (400 ms) and
// short-term (3 s) loudness, plus the sample peak over the short-term window.
// Runs on the worker thread only.
class LoudnessMeter
{
public:
    // K-weighting stages for any sample rate (exactly the BS.1770 table at 48 kHz).
    static BiquadCoefficients makeShelfStage (double sampleRate) noexcept;
    static BiquadCoefficients makeHighPassStage (double sampleRate) noexcept;

    void prepare (double sampleRate);
    void reset() noexcept;

    void process (const float* left, const float* right, int numSamples) noexcept;

    float getMomentaryLufs() const noexcept;   // −inf when silent
    float getShortTermLufs() const noexcept;   // −inf when silent
    float getShortTermPeakDb() const noexcept; // dBFS, −inf when silent

private:
    struct Biquad
    {
        BiquadCoefficients c {};
        double z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const auto y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };

    static constexpr int subBlocksShortTerm = 30; // 30 × 100 ms = 3 s
    static constexpr int subBlocksMomentary = 4;  //  4 × 100 ms = 400 ms

    float meanLufs (int numSubBlocks) const noexcept;

    std::array<Biquad, 2> shelf, highPass;

    int subBlockLength = 4800;
    int subBlockFill = 0;
    double subBlockEnergy = 0.0;
    float subBlockPeak = 0.0f;

    std::array<double, subBlocksShortTerm> energies {}; // channel-summed mean square per sub-block
    std::array<float, subBlocksShortTerm> peaks {};
    int writeIndex = 0;
    int numFilled = 0;
};

} // namespace breark
