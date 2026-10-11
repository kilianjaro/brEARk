#include "GridLoopDetector.h"
#include "ModelConstants.h"

#include <cmath>

namespace breark
{
namespace
{
constexpr int ringSize = 1 << 15;  // 32768 frames: two audio chunks + one FFT at 192 kHz
constexpr int audioChunk = 4096;   // analyse slots after every chunk, so the ring stays small
constexpr int slotCapacity = 1100; // 2 × 16 bars of 16ths, up to 8/4 time
} // namespace

void GridLoopDetector::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // ~46 ms window at any sample rate.
    const auto order = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fft = std::make_unique<RealFft> (order);
    fftSize = fft->getSize();

    // Fresh vectors: resize() would keep the capacity of a higher rate.
    window = std::vector<float> ((size_t) fftSize);

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / fftSize));

    fftBuffer = std::vector<float> ((size_t) fftSize * 2, 0.0f);

    const auto numBins = fftSize / 2 + 1;

    for (int b = 0; b <= numLoopBands; ++b)
    {
        const auto edgeHz = std::min (100.0 * std::pow (120.0, (double) b / numLoopBands), sampleRate / 2.0);
        bandEdgeBins[(size_t) b] = std::min (numBins, (int) std::ceil (edgeHz * fftSize / sampleRate));
    }

    ring.assign ((size_t) ringSize, 0.0f);
    slots.assign ((size_t) slotCapacity, BandFrame {});
    stamps.reserve (8192);
    pendingCentres.reserve (512);
    reset();
}

void GridLoopDetector::reset() noexcept
{
    streamEnd = validFrom = scheduledUntil = 0;
    stamps.clear();
    pendingCentres.clear();
    breakSequence();
}

void GridLoopDetector::breakSequence() noexcept
{
    slotWrite = slotCount = newSlots = 0;
    pendingCentres.clear(); // audio from before the break must not join the new sequence
    endLoop();
}

void GridLoopDetector::endLoop() noexcept
{
    loopingSeconds = missSeconds = lastLoopSeconds = 0.0;
    loopBars = bestBars = 0;
    best = {};
}

//==============================================================================
void GridLoopDetector::addStamp (const BlockStamp& stamp)
{
    stamps.push_back (stamp);
    scheduleSlots();
    analyseReadySlots();
}

void GridLoopDetector::scheduleSlots()
{
    // A stamp's block runs until the next stamp; only closed blocks are scheduled.
    size_t i = 0;

    for (; i + 1 < stamps.size(); ++i)
    {
        const auto& s = stamps[i];
        const auto start = std::max (s.streamPosition, scheduledUntil);
        const auto end = stamps[i + 1].streamPosition;

        if (! s.playing || s.bpm <= 0.0)
        {
            breakSequence(); // no grid: a loop can't continue across a stop
        }
        else if (end > start)
        {
            const auto barSlots = std::max (1, (int) std::lround (s.barLengthQuarters / model::loopSlotQuarters));

            if (barSlots != slotsPerBar)
            {
                slotsPerBar = barSlots;
                breakSequence();
            }

            const auto samplesPerQuarter = 60.0 * sampleRate / s.bpm;
            slotSeconds = model::loopSlotQuarters * 60.0 / s.bpm;

            const auto ppqStart = s.ppq + (double) (start - s.streamPosition) / samplesPerQuarter;
            const auto ppqEnd = s.ppq + (double) (end - s.streamPosition) / samplesPerQuarter;

            // Slot centres sit in the middle of each 16th: (k + ½) · ¼ quarter.
            for (auto k = std::ceil (ppqStart / model::loopSlotQuarters - 0.5); (k + 0.5) * model::loopSlotQuarters < ppqEnd; k += 1.0)
                pendingCentres.push_back ((double) s.streamPosition + ((k + 0.5) * model::loopSlotQuarters - s.ppq) * samplesPerQuarter);
        }

        scheduledUntil = std::max (scheduledUntil, end);
    }

    stamps.erase (stamps.begin(), stamps.begin() + (std::ptrdiff_t) i);
}

void GridLoopDetector::addAudio (const float* left, const float* right, int numFrames, juce::int64 streamPosition)
{
    if (ring.empty())
        return;

    if (streamPosition != streamEnd) // frames were skipped (export, reconfiguration)
    {
        validFrom = streamPosition;
        breakSequence();
    }

    for (int done = 0; done < numFrames;)
    {
        const auto n = std::min (audioChunk, numFrames - done);

        for (int i = 0; i < n; ++i)
            ring[(size_t) ((streamPosition + done + i) & (ringSize - 1))] = 0.5f * (left[done + i] + right[done + i]);

        done += n;
        streamEnd = streamPosition + done;
        analyseReadySlots();
    }
}

void GridLoopDetector::analyseReadySlots()
{
    const auto numBins = fftSize / 2 + 1;
    size_t i = 0;

    for (; i < pendingCentres.size(); ++i)
    {
        const auto first = (juce::int64) std::llround (pendingCentres[i]) - fftSize / 2;

        if (first + fftSize > streamEnd)
            break; // its audio hasn't arrived yet

        if (first < validFrom || first < streamEnd - ringSize + audioChunk)
            continue; // its audio is gone

        for (int n = 0; n < fftSize; ++n)
            fftBuffer[(size_t) n] = ring[(size_t) ((first + n) & (ringSize - 1))] * window[(size_t) n];

        std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fftBuffer.data());

        BandFrame bands {};

        for (int b = 0; b < numLoopBands; ++b)
        {
            double energy = 1.0e-12;

            for (int k = bandEdgeBins[(size_t) b]; k < std::min (bandEdgeBins[(size_t) b + 1], numBins); ++k)
                energy += (double) fftBuffer[(size_t) (2 * k)] * fftBuffer[(size_t) (2 * k)]
                        + (double) fftBuffer[(size_t) (2 * k + 1)] * fftBuffer[(size_t) (2 * k + 1)];

            bands[(size_t) b] = (float) (10.0 * std::log10 (energy));
        }

        slots[(size_t) slotWrite] = bands;
        slotWrite = (slotWrite + 1) % slotCapacity;
        slotCount = std::min (slotCount + 1, slotCapacity);
        ++newSlots;
    }

    pendingCentres.erase (pendingCentres.begin(), pendingCentres.begin() + (std::ptrdiff_t) i);
}

//==============================================================================
const BandFrame& GridLoopDetector::slotsAgo (int count) const noexcept
{
    return slots[(size_t) ((slotWrite - 1 - count + 2 * slotCapacity) % slotCapacity)];
}

GridLoopDetector::Match GridLoopDetector::compare (int lag, int windowSlots) const noexcept
{
    // Two independent questions about "now" vs "one loop earlier":
    //   rhythm: do the same things happen on the same 16ths? Correlation of
    //           the 16th-to-16th changes per band (so slow drift from LFOs, EQ
    //           or faders doesn't matter), averaged over bands;
    //   shift:  did the band levels change? RMS of the per-band level change
    //           after removing the common offset (a fader move). A chord
    //           change moves energy between bands; an LFO barely does.
    // Bands far below the loudest one are numerical noise and are skipped.
    std::array<double, numLoopBands> meanNow {}, meanThen {};

    for (int i = 0; i < windowSlots; ++i)
    {
        for (size_t b = 0; b < (size_t) numLoopBands; ++b)
        {
            meanNow[b] += slotsAgo (i)[b];
            meanThen[b] += slotsAgo (i + lag)[b];
        }
    }

    auto loudest = -1.0e9;

    for (size_t b = 0; b < (size_t) numLoopBands; ++b)
    {
        meanNow[b] /= windowSlots;
        meanThen[b] /= windowSlots;
        loudest = std::max (loudest, 0.5 * (meanNow[b] + meanThen[b]));
    }

    std::array<bool, numLoopBands> audible {};
    double offset = 0.0;
    int audibleBands = 0;

    for (size_t b = 0; b < (size_t) numLoopBands; ++b)
    {
        audible[b] = 0.5 * (meanNow[b] + meanThen[b]) > loudest - model::loopAudibleRangeDb;

        if (audible[b])
        {
            offset += meanNow[b] - meanThen[b];
            ++audibleBands;
        }
    }

    if (audibleBands == 0)
        return {};

    offset /= audibleBands;

    double rhythmTotal = 0.0, shiftSquares = 0.0;
    int movingBands = 0;
    const auto minVariance = (windowSlots - 1) * (double) model::loopRhythmMinChangeDb * model::loopRhythmMinChangeDb;

    for (size_t b = 0; b < (size_t) numLoopBands; ++b)
    {
        if (! audible[b])
            continue;

        const auto shift = meanNow[b] - meanThen[b] - offset;
        shiftSquares += shift * shift;

        // Rhythm lives in the changes from one 16th to the next: hits, onsets,
        // the sidechain pump. Comparing changes rather than levels ignores slow
        // drift: an unsynced LFO moves a band by well under 0.5 dB per 16th.
        double cross = 0.0, varNow = 0.0, varThen = 0.0;

        for (int i = 0; i + 1 < windowSlots; ++i)
        {
            const auto x = slotsAgo (i)[b] - slotsAgo (i + 1)[b];
            const auto y = slotsAgo (i + lag)[b] - slotsAgo (i + lag + 1)[b];
            cross += x * y;
            varNow += x * x;
            varThen += y * y;
        }

        if (varNow < minVariance || varThen < minVariance)
            continue; // no rhythm in this band (a sustained pad, a drone): only its level counts

        rhythmTotal += cross / std::sqrt (varNow * varThen);
        ++movingBands;
    }

    Match match;
    match.rhythm = movingBands >= model::loopMinMovingBands ? (float) (rhythmTotal / movingBands) : 0.0f;
    match.shiftDb = (float) std::sqrt (shiftSquares / audibleBands);
    return match;
}

void GridLoopDetector::update (double seconds, bool silent)
{
    const auto fresh = newSlots > 0;
    newSlots = 0;

    if (silent || ! fresh)
    {
        endLoop();
        return;
    }

    // Try every loop length from 1 to 16 bars; the shortest match is the loop.
    // At least 4 bars (~7 s) are compared: short loops are checked over several
    // passes, and the level difference of a periodic LFO averages out.
    auto found = 0;
    best = {};
    bestBars = 0;

    for (int bars = 1; bars <= model::loopMaxBars; ++bars)
    {
        const auto lag = bars * slotsPerBar;
        const auto windowSlots = std::max (model::loopMinWindowBars, bars) * slotsPerBar;

        if (lag + windowSlots + 1 > slotCount)
            break;

        const auto m = compare (lag, windowSlots);
        const auto isLoop = m.rhythm >= model::loopMinRhythm && m.shiftDb <= model::loopMaxShiftDb;

        // Diagnostics: the shortest loop found, otherwise the closest miss.
        if ((isLoop && found == 0) || (found == 0 && m.rhythm - 0.05f * m.shiftDb > best.rhythm - 0.05f * best.shiftDb))
        {
            best = m;
            bestBars = bars;
        }

        if (found == 0 && isLoop)
            found = bars;
    }

    if (found > 0)
    {
        // A fresh detection has already played one full repeat.
        loopingSeconds = loopingSeconds > 0.0 ? loopingSeconds + seconds : found * slotsPerBar * slotSeconds;
        loopBars = found;
        lastLoopSeconds = found * slotsPerBar * slotSeconds;
        missSeconds = 0.0;
        return;
    }

    // No match: a quick edit pauses the count; a real change ends it. A tweak
    // (EQ, mute) mismatches for one loop length, then matches again.
    const auto grace = std::max (model::loopGraceSeconds, lastLoopSeconds + model::loopGraceSeconds / 2.0);
    loopBars = 0;

    if ((missSeconds += seconds) > grace)
    {
        loopingSeconds = 0.0;
        missSeconds = 0.0;
    }
}

} // namespace breark
