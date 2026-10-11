#pragma once

#include "SampleFifo.h"

#include "RealFft.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace breark
{
inline constexpr int numLoopBands = 12;          // log-spaced, 100 Hz–12 kHz
using BandFrame = std::array<float, numLoopBands>; // energy per band, dB

//==============================================================================
// Detects loops on the host's tempo grid.
//
// Every 16th note of playback, one FFT is taken exactly centred on the grid
// position, so each pass of a loop is analysed at the same musical moments:
// no misalignment, no smoothing, the rhythm stays in the data. Then the last
// bars are compared with the bars 1, 2, 3 … 16 bars earlier, in played order,
// so an Arrangement loop brace (the playhead jumping back) looks just like a
// Session View clip looping.
//
// A loop needs two things (see compare()): the same rhythm in every band,
// regardless of slow level drift (LFOs, EQ, faders), and band levels that
// haven't shifted much (so "same drums, new chords" is not a loop). Bands that
// don't move are ignored, and with too few of them (a drone) nothing is a loop.
//
// Worker thread only. Feed stamps before the audio they describe.
class GridLoopDetector
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    void addStamp (const BlockStamp& stamp);
    void addAudio (const float* left, const float* right, int numFrames, juce::int64 streamPosition);

    // Once per model tick. Silence, or no grid (transport stopped), ends a loop.
    void update (double seconds, bool silent);

    double getLoopingSeconds() const noexcept { return loopingSeconds; }
    int getLoopBars() const noexcept { return loopBars; }

    // Diagnostics: the loop found by the last check, or the closest miss.
    float getBestRhythm() const noexcept { return best.rhythm; }
    float getBestShiftDb() const noexcept { return best.shiftDb; }
    int getBestBars() const noexcept { return bestBars; }
    bool hasGrid() const noexcept { return slotCount > 0; }

private:
    void scheduleSlots();
    void analyseReadySlots();
    void breakSequence() noexcept;
    void endLoop() noexcept;
    const BandFrame& slotsAgo (int slots) const noexcept;
    struct Match
    {
        float rhythm = 0.0f;   // 1 = the same things on the same 16ths
        float shiftDb = 99.0f; // RMS change of band levels, fader removed
    };

    Match compare (int lagSlots, int windowSlots) const noexcept;

    double sampleRate = 44100.0;
    std::unique_ptr<RealFft> fft;
    int fftSize = 0;
    std::vector<float> window, fftBuffer;
    std::array<int, numLoopBands + 1> bandEdgeBins {};

    // Mono audio of the recent stream, indexed by stream position.
    std::vector<float> ring;
    juce::int64 streamEnd = 0;  // position after the newest frame
    juce::int64 validFrom = 0;  // older positions are stale (gap in the stream)

    std::vector<BlockStamp> stamps;       // the last one is still open
    juce::int64 scheduledUntil = 0;
    std::vector<double> pendingCentres;   // grid positions waiting for their audio
    int slotsPerBar = 16;
    double slotSeconds = 0.125;

    std::vector<BandFrame> slots;         // analysed 16ths, in played order
    int slotWrite = 0, slotCount = 0, newSlots = 0;

    double loopingSeconds = 0.0, missSeconds = 0.0, lastLoopSeconds = 0.0;
    int loopBars = 0, bestBars = 0;
    Match best;
};

} // namespace breark
