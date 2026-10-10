#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace breark
{
// Energy in 12 log-spaced bands, 100 Hz–12 kHz, for one analysed frame.
inline constexpr int numLoopBands = 12;
using BandFrame = std::array<float, numLoopBands>;

//==============================================================================
// One FFT frame every ~43 ms (Apple vDSP), summed over both channels.
// Each frame yields mean-square powers (Parseval, window-corrected):
// A-weighted (for the hearing dose), flat 20 Hz–20 kHz, body 250 Hz–2 kHz,
// presence 2–5 kHz and highs 8–16 kHz, plus a BandFrame for loop detection.
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

    // Hands every BandFrame analysed since the last call to consumer (const BandFrame&).
    template <typename Consumer>
    void drainBandFrames (Consumer&& consumer)
    {
        for (const auto& frame : bandFrames)
            consumer (frame);

        bandFrames.clear();
    }

    int getFftSize() const noexcept { return fftSize; }
    double getAnalysedFrameSeconds() const noexcept { return analysedFrameSeconds; }

private:
    void analyseFrame();
    double weightedSum (const std::vector<float>& table) const noexcept;

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 0;
    int frameStride = 1;
    int fill = 0;
    int frameCounter = 0;
    double analysedFrameSeconds = 0.0;

    std::vector<float> window, frameLeft, frameRight, fftBuffer, power;
    std::vector<float> aTable, flatTable, bodyTable, presenceTable, highsTable;
    std::array<int, numLoopBands + 1> bandEdgeBins {};
    std::vector<BandFrame> bandFrames;

    Totals totals;
};

} // namespace breark
