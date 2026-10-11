#pragma once

#include <Accelerate/Accelerate.h>

namespace breark
{
//==============================================================================
// Real-input forward FFT on Apple's vDSP: the same transform, layout and
// scaling as juce::dsp::FFT::performRealOnlyForwardTransform (…, true).
//
// It exists for memory. JUCE asks vDSP for a radix-5 setup
// (vDSP_create_fftsetup (order, 2), and 2 is kFFTRadix5), 5.4× the size of
// the radix-2 setup a power-of-two FFT needs: 0.86 instead of 0.16 MB at 2048
// points, 3.2 instead of 0.6 MB at 8192. brEARk keeps two FFTs alive.
class RealFft
{
public:
    explicit RealFft (int orderToUse)
        : order (orderToUse),
          size (1 << orderToUse),
          setup (vDSP_create_fftsetup ((vDSP_Length) orderToUse, kFFTRadix2))
    {
    }

    ~RealFft()
    {
        if (setup != nullptr)
            vDSP_destroy_fftsetup (setup);
    }

    RealFft (const RealFft&) = delete;
    RealFft& operator= (const RealFft&) = delete;

    int getSize() const noexcept { return size; }

    // data holds 2 × size floats, the first size of them the input samples.
    // Afterwards data[2k], data[2k + 1] are the real and imaginary parts of
    // bin k, for k = 0 … size / 2, at the true DFT scale.
    void performRealOnlyForwardTransform (float* data) const noexcept
    {
        DSPSplitComplex split { data, data + 1 };
        data[size] = 0.0f;
        vDSP_fft_zrip (setup, &split, 2, (vDSP_Length) order, kFFTDirection_Forward);

        const auto half = 0.5f; // vDSP's real FFT returns twice the DFT
        vDSP_vsmul (data, 1, &half, data, 1, (vDSP_Length) (size * 2));

        // vDSP packs the (real) Nyquist bin into the imaginary part of DC.
        data[size] = data[1];
        data[size + 1] = 0.0f;
        data[1] = 0.0f;
    }

private:
    int order, size;
    FFTSetup setup;
};

} // namespace breark
