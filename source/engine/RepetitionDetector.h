#pragma once

#include "SpectrumAnalyser.h"

#include <vector>

namespace breark
{
//==============================================================================
// Detects continuous playback of the same audio, like a looping clip in
// Session View: the last few seconds resemble the audio exactly one period
// earlier, for any period between 1.5 and 32 s.
//
// Features are log band energies smoothed over ~370 ms (robust to the loop
// length not being a whole number of frames). Windows are compared by
// band-wise correlation: each band's mean is removed, so fader moves and EQ
// tweaks don't break a loop, and humanised hits (random velocity, modulation)
// still match. Static sounds (drones, noise) never count. Worker thread only.
class RepetitionDetector
{
public:
    void prepare (double analysedFrameSeconds);
    void reset() noexcept;

    void addFrame (const BandFrame& bandPowers);

    // Once per model tick. Silence, or no new audio, ends a loop.
    void update (double seconds, bool silent);

    double getLoopingSeconds() const noexcept { return loopingSeconds; }
    double getLoopLengthSeconds() const noexcept { return currentLag * frameSeconds; }

    // Diagnostics: the best match found by the last check, looping or not.
    float getBestSimilarity() const noexcept { return bestSimilarity; }
    double getBestLagSeconds() const noexcept { return bestLag * frameSeconds; }

private:
    void endLoop() noexcept;
    const BandFrame& framesAgo (int frames) const noexcept;
    float similarity (int lag, int step) const noexcept;
    bool hasMovement() const noexcept;
    int search();
    bool verify (int& lag) const noexcept;

    double frameSeconds = 0.0;
    int window = 0, minLag = 0, maxLag = 0;

    std::vector<BandFrame> raw;      // last few raw frames, for smoothing
    std::vector<BandFrame> features; // smoothed log energies, ring buffer
    int rawPosition = 0, writePosition = 0, filled = 0, framesSinceUpdate = 0;

    int currentLag = 0;
    double loopingSeconds = 0.0, secondsSinceSearch = 0.0, missSeconds = 0.0;
    float bestSimilarity = 0.0f;
    int bestLag = 0;
};

} // namespace breark
