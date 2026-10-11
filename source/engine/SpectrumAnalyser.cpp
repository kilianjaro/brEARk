#include "SpectrumAnalyser.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace breark
{
double SpectrumAnalyser::aWeightingPowerGain (double frequencyHz) noexcept
{
    const auto f2 = frequencyHz * frequencyHz;
    constexpr auto c1 = 20.6 * 20.6, c2 = 107.7 * 107.7, c3 = 737.9 * 737.9, c4 = 12194.0 * 12194.0;

    const auto ra = (c4 * f2 * f2) / ((f2 + c1) * std::sqrt ((f2 + c2) * (f2 + c3)) * (f2 + c4));

    return ra * ra * std::pow (10.0, 0.2); // +2.00 dB normalisation
}

void SpectrumAnalyser::prepare (double sampleRate)
{
    // Frames stay ~43 ms long; the stride keeps analysed frames per second constant.
    const auto order = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fft = std::make_unique<RealFft> (order);
    fftSize = fft->getSize();
    frameStride = fftSize / 2048;

    // Fresh vectors: resize() would keep the capacity of a higher rate.
    const auto numBins = fftSize / 2 + 1;
    window = std::vector<float> ((size_t) fftSize);
    frameLeft = std::vector<float> ((size_t) fftSize, 0.0f);
    frameRight = std::vector<float> ((size_t) fftSize, 0.0f);
    fftBuffer = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    power = std::vector<float> ((size_t) numBins, 0.0f);

    double windowEnergy = 0.0;

    for (int n = 0; n < fftSize; ++n)
    {
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / fftSize)); // periodic Hann
        windowEnergy += (double) window[(size_t) n] * window[(size_t) n];
    }

    // Parseval for a one-sided spectrum: mean square ≈ Σ c_k |X_k|² / (N · Σw²),
    // with c_k = 1 at DC and Nyquist, 2 elsewhere.
    const auto norm = 1.0 / (fftSize * windowEnergy);
    const auto nyquist = sampleRate / 2.0;

    auto makeTable = [&] (auto&& weight)
    {
        std::vector<float> table ((size_t) numBins);

        for (int k = 0; k < numBins; ++k)
        {
            const auto c = (k == 0 || k == numBins - 1) ? 1.0 : 2.0;
            table[(size_t) k] = (float) (c * norm * weight (k * sampleRate / fftSize));
        }

        return table;
    };

    auto band = [nyquist] (double low, double high)
    {
        return [low, top = std::min (high, nyquist)] (double f) { return (f >= low && f < top) ? 1.0 : 0.0; };
    };

    aTable        = makeTable ([] (double f) { return aWeightingPowerGain (f); });
    flatTable     = makeTable (band (20.0, 20000.0));
    bodyTable     = makeTable (band (250.0, 2000.0));
    presenceTable = makeTable (band (2000.0, 5000.0));
    highsTable    = makeTable (band (8000.0, 16000.0));

    reset();
}

void SpectrumAnalyser::reset() noexcept
{
    fill = 0;
    frameCounter = 0;
    totals = {};
}

void SpectrumAnalyser::process (const float* left, const float* right, int numSamples)
{
    while (numSamples > 0)
    {
        const auto n = std::min (numSamples, fftSize - fill);
        std::copy (left, left + n, frameLeft.begin() + fill);
        std::copy (right, right + n, frameRight.begin() + fill);

        fill += n;
        left += n;
        right += n;
        numSamples -= n;

        if (fill == fftSize)
        {
            if (frameCounter++ % frameStride == 0)
                analyseFrame();

            fill = 0;
        }
    }
}

void SpectrumAnalyser::analyseFrame() noexcept
{
    const auto numBins = fftSize / 2 + 1;
    auto first = true;

    for (const auto* frame : { &frameLeft, &frameRight })
    {
        juce::FloatVectorOperations::multiply (fftBuffer.data(), frame->data(), window.data(), fftSize);
        std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

        fft->performRealOnlyForwardTransform (fftBuffer.data());

        for (int k = 0; k < numBins; ++k)
        {
            const auto re = fftBuffer[(size_t) (2 * k)];
            const auto im = fftBuffer[(size_t) (2 * k + 1)];
            power[(size_t) k] = (first ? 0.0f : power[(size_t) k]) + re * re + im * im;
        }

        first = false;
    }

    totals.aWeighted += weightedSum (aTable);
    totals.flat      += weightedSum (flatTable);
    totals.body      += weightedSum (bodyTable);
    totals.presence  += weightedSum (presenceTable);
    totals.highs     += weightedSum (highsTable);
    ++totals.frames;
}

double SpectrumAnalyser::weightedSum (const std::vector<float>& table) const noexcept
{
    double sum = 0.0;

    for (size_t k = 0; k < power.size(); ++k)
        sum += (double) power[k] * table[k];

    return sum;
}

SpectrumAnalyser::Totals SpectrumAnalyser::takeTotals() noexcept
{
    return std::exchange (totals, {});
}

} // namespace breark
