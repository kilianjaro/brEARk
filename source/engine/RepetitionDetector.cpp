#include "RepetitionDetector.h"
#include "ModelConstants.h"

#include <cmath>

namespace breark
{
void RepetitionDetector::prepare (double analysedFrameSeconds)
{
    frameSeconds = analysedFrameSeconds;
    window = std::max (8, (int) std::lround (model::loopWindowSeconds / frameSeconds));
    minLag = std::max (1, (int) std::lround (model::loopMinLengthSeconds / frameSeconds));
    maxLag = (int) std::lround (model::loopMaxLengthSeconds / frameSeconds);

    raw.assign ((size_t) model::loopSmoothingFrames, BandFrame {});
    features.assign ((size_t) (maxLag + window + 2), BandFrame {});
    reset();
}

void RepetitionDetector::reset() noexcept
{
    rawPosition = writePosition = filled = framesSinceUpdate = 0;
    endLoop();
}

void RepetitionDetector::endLoop() noexcept
{
    currentLag = 0;
    loopingSeconds = secondsSinceSearch = missSeconds = 0.0;
    lastDeviationDb = std::numeric_limits<float>::infinity();
}

void RepetitionDetector::addFrame (const BandFrame& bandPowers)
{
    raw[(size_t) rawPosition] = bandPowers;
    rawPosition = (rawPosition + 1) % (int) raw.size();

    BandFrame smoothed {};

    for (const auto& frame : raw)
        for (size_t b = 0; b < smoothed.size(); ++b)
            smoothed[b] += frame[b];

    for (auto& energy : smoothed)
        energy = 10.0f * std::log10 (energy + 1.0e-12f);

    features[(size_t) writePosition] = smoothed;
    writePosition = (writePosition + 1) % (int) features.size();
    filled = std::min (filled + 1, (int) features.size());
    ++framesSinceUpdate;
}

const BandFrame& RepetitionDetector::framesAgo (int frames) const noexcept
{
    const auto size = (int) features.size();
    return features[(size_t) ((writePosition - 1 - frames + 2 * size) % size)];
}

float RepetitionDetector::deviationDb (int lag, int step) const noexcept
{
    // Mean absolute difference after removing the mean difference (level-invariant).
    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < window; i += step)
        for (size_t b = 0; b < (size_t) numLoopBands; ++b, ++count)
            sum += framesAgo (i)[b] - framesAgo (i + lag)[b];

    const auto mean = sum / count;
    double deviation = 0.0;

    for (int i = 0; i < window; i += step)
        for (size_t b = 0; b < (size_t) numLoopBands; ++b)
            deviation += std::abs (framesAgo (i)[b] - framesAgo (i + lag)[b] - mean);

    return (float) (deviation / count);
}

bool RepetitionDetector::hasMovement() const noexcept
{
    // Average over bands of the standard deviation over time, in dB.
    double total = 0.0;

    for (size_t b = 0; b < (size_t) numLoopBands; ++b)
    {
        double sum = 0.0, squares = 0.0;

        for (int i = 0; i < window; ++i)
        {
            sum += framesAgo (i)[b];
            squares += framesAgo (i)[b] * framesAgo (i)[b];
        }

        const auto mean = sum / window;
        total += std::sqrt (std::max (0.0, squares / window - mean * mean));
    }

    return total / numLoopBands >= model::loopMinMovementDb;
}

int RepetitionDetector::search() const noexcept
{
    // Coarse pass on every 4th frame finds the best lag, then a full check
    // around it. Prefers the shortest period that matches as well as the best.
    const auto longest = std::min (maxLag, filled - window - 1);
    auto bestLag = 0;
    auto bestDeviation = std::numeric_limits<float>::infinity();

    for (int lag = minLag; lag <= longest; ++lag)
    {
        if (const auto d = deviationDb (lag, 4); d < bestDeviation)
        {
            bestDeviation = d;
            bestLag = lag;
        }
    }

    if (bestLag == 0 || bestDeviation > 2.0f * model::loopMatchThresholdDb)
        return 0;

    for (int lag = minLag; lag <= longest; ++lag)
        if (deviationDb (lag, 4) <= bestDeviation + 0.2f)
            if (auto candidate = lag; verify (candidate))
                return candidate;

    return 0;
}

bool RepetitionDetector::verify (int& lag) const noexcept
{
    // The loop length is rarely a whole number of frames: check the neighbours too.
    auto best = std::numeric_limits<float>::infinity();
    auto bestLag = lag;

    for (int candidate = lag - 1; candidate <= lag + 1; ++candidate)
    {
        if (candidate < minLag || candidate + window >= filled)
            continue;

        if (const auto d = deviationDb (candidate, 1); d < best)
        {
            best = d;
            bestLag = candidate;
        }
    }

    if (best > model::loopMatchThresholdDb)
        return false;

    lag = bestLag;
    return true;
}

void RepetitionDetector::update (double seconds, bool silent)
{
    const auto freshAudio = framesSinceUpdate > 0;
    framesSinceUpdate = 0;

    // Stopping playback ends a loop at once; the history is kept.
    if (silent || ! freshAudio || filled < minLag + window + 1)
    {
        endLoop();
        return;
    }

    const auto moving = hasMovement();

    if (moving && currentLag > 0 && verify (currentLag))
    {
        loopingSeconds += seconds;
        missSeconds = 0.0;
        lastDeviationDb = deviationDb (currentLag, 1);
        return;
    }

    // Not looping: search every few seconds. A loop just lost: search now.
    if (loopingSeconds <= 0.0 && (secondsSinceSearch += seconds) < model::loopSearchIntervalSeconds)
        return;

    secondsSinceSearch = 0.0;

    if (const auto found = moving ? search() : 0; found > 0)
    {
        // A fresh detection has already been repeating for one period plus the window.
        loopingSeconds = loopingSeconds > 0.0 ? loopingSeconds + seconds : (found + window) * frameSeconds;
        currentLag = found;
        missSeconds = 0.0;
        lastDeviationDb = deviationDb (found, 1);
        return;
    }

    // No match: a fader move or quick edit pauses the count; a real change ends it.
    currentLag = 0;
    lastDeviationDb = std::numeric_limits<float>::infinity();

    if ((missSeconds += seconds) > model::loopGraceSeconds)
    {
        loopingSeconds = 0.0;
        missSeconds = 0.0;
    }
}

} // namespace breark
