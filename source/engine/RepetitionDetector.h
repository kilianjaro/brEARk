#pragma once

#include "SpectrumAnalyser.h"

#include <limits>
#include <vector>

namespace breark
{
//==============================================================================
// Detects continuous playback of identical audio, like a looping clip in
// Session View: the last few seconds match the audio exactly one period
// earlier, for any period between 1.5 and 32 s.
//
// Features are log band energies smoothed over ~370 ms (robust to the loop
// length not being a whole number of frames). The comparison removes the
// mean difference, so fader moves don't break a loop, and static sounds
// (drones, noise) never count. Runs on the worker thread only.
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
    float getLastDeviationDb() const noexcept { return lastDeviationDb; } // diagnostics

private:
    void endLoop() noexcept;
    const BandFrame& framesAgo (int frames) const noexcept;
    float deviationDb (int lag, int step) const noexcept;
    bool hasMovement() const noexcept;
    int search() const noexcept;
    bool verify (int& lag) const noexcept;

    double frameSeconds = 0.0;
    int window = 0, minLag = 0, maxLag = 0;

    std::vector<BandFrame> raw;      // last few raw frames, for smoothing
    std::vector<BandFrame> features; // smoothed log energies, ring buffer
    int rawPosition = 0, writePosition = 0, filled = 0, framesSinceUpdate = 0;

    int currentLag = 0;
    double loopingSeconds = 0.0, secondsSinceSearch = 0.0, missSeconds = 0.0;
    float lastDeviationDb = std::numeric_limits<float>::infinity();
};

} // namespace breark
