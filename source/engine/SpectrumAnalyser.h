#pragma once

#include "RealFft.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace breark
{
//==============================================================================
// One FFT frame every ~43 ms (Apple vDSP), summed over both channels.
// Each frame yields mean-square powers (Parseval, window-corrected):
// A-weighted (for the hearing dose), flat 20 Hz–20 kHz, body 250 Hz–2 kHz,
// presence 2–5 kHz and highs 8–16 kHz.
// At high sample rates only every 2nd/4th frame is analysed, so CPU cost
// stays constant. Runs on the worker thread only.
class SpectrumAnalyser
{
public:
    struct Totals
    {
        double aWeighted = 0.0; // Σ per-frame mean square, A-weighted
        double flat = 0.0;      // Σ per-frame mean square, 20 Hz–20 kHz
        double body = 0.0;      // Σ per-frame mean square, 250 Hz–2 kHz
        double presence = 0.0;  // Σ per-frame mean square, 2–5 kHz
        double highs = 0.0;     // Σ per-frame mean square, 8–16 kHz
        int frames = 0;
    };

    // IEC 61672 A-weighting as a linear power gain (1.0 at 1 kHz).
    static double aWeightingPowerGain (double frequencyHz) noexcept;

    void prepare (double sampleRate);
    void reset() noexcept;

    void process (const float* left, const float* right, int numSamples);

    // Returns the sums since the previous call and starts new ones.
    Totals takeTotals() noexcept;

    int getFftSize() const noexcept { return fftSize; }

private:
    void analyseFrame() noexcept;
    double weightedSum (const std::vector<float>& table) const noexcept;

    std::unique_ptr<RealFft> fft;
    int fftSize = 0;
    int frameStride = 1;
    int fill = 0;
    int frameCounter = 0;

    std::vector<float> window, frameLeft, frameRight, fftBuffer, power;
    std::vector<float> aTable, flatTable, bodyTable, presenceTable, highsTable;

    Totals totals;
};

} // namespace breark
