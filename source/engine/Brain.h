#pragma once

#include "BreakController.h"
#include "ExposureModel.h"
#include "HostActivity.h"
#include "LevelCreepTracker.h"
#include "LoudnessMeter.h"
#include "LoudnessWindow.h"
#include "Persistence.h"
#include "PlayheadTracker.h"
#include "GridLoopDetector.h"
#include "SampleFifo.h"
#include "Snapshot.h"
#include "SpectrumAnalyser.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace breark
{
//==============================================================================
// One Brain per Live process, shared by all brEARk instances through a
// juce::SharedResourcePointer. The first instance is the active one: only it
// feeds audio in, so nothing is ever counted twice.
//
// Threads:
//   audio thread  → isActive(), push(), pushBypassed(), updateTransport(),
//                   addAudioThreadTicks()                                         (lock-free)
//   worker thread → run(): analysis on a background-priority thread
//   message / UI  → attach(), detach(), prepare(), getSnapshot()
class Brain final : private juce::Thread
{
public:
    Brain() : Brain (persistence::defaultFile()) {}
    explicit Brain (juce::File stateFileToUse); // tests pass a temporary file
    ~Brain() override;

    //==============================================================================
    // Message thread
    void attach (const void* instance);
    void detach (const void* instance);
    void prepare (double sampleRate) noexcept;

    void setSettings (Settings newSettings) noexcept;
    Settings getSettings() const noexcept;

    // Debug: the model clock runs this many times faster. While > 1 nothing is
    // saved, and returning to 1 restores the real state from disk.
    void setTimeWarp (int factor) noexcept { timeWarp.store (juce::jmax (1, factor)); }

    // The BREAK button (toast or plugin window): starts a guided break, or ends it.
    void toggleBreak() noexcept { breakToggleRequested.store (true); }

    // HARD, unplugged: 10 more minutes.
    void snooze() noexcept { snoozeRequested.store (true); }

    // NOT THAT LOUD: what plays now (last 2 minutes) is your normal level.
    void notThatLoud() noexcept { notThatLoudRequested.store (true); }

    // VOLUME: you turned your monitors down (negative dB) or up (M10).
    void adjustMonitorVolume (int db) noexcept { monitorVolumeRequestDb.fetch_add (db); }

    // Alerts produced since the last call, for the message thread to present.
    std::vector<Alert> takeAlerts();

    //==============================================================================
    // Audio thread: wait-free, allocation-free, never writes the buffer.
    bool isActive (const void* instance) const noexcept
    {
        return active.load (std::memory_order_acquire) == instance;
    }

    template <typename SampleType>
    void push (const juce::AudioBuffer<SampleType>& buffer, bool hostIsNonRealtime, const BlockStamp& stamp = {}) noexcept
    {
        samplesProcessed.fetch_add (buffer.getNumSamples(), std::memory_order_relaxed);

        // Exports never reach the ears, so they are never analysed.
        if (hostIsNonRealtime)
        {
            hostNonRealtimeSeen.store (true, std::memory_order_relaxed);
            return;
        }

        fifo.push (buffer, stamp);
    }

    // Switched off in Live (the host calls processBlockBypassed): the model
    // freezes while this keeps coming. Only the count is needed.
    void pushBypassed (int numSamples) noexcept
    {
        samplesBypassed.fetch_add (numSamples, std::memory_order_relaxed);
    }

    // Playhead position for the bar-repetition count; a few atomic stores.
    void updateTransport (const juce::AudioPlayHead::PositionInfo& position) noexcept
    {
        transportPlaying.store (position.getIsPlaying(), std::memory_order_relaxed);
        transportRecording.store (position.getIsRecording(), std::memory_order_relaxed);

        if (const auto ppq = position.getPpqPosition())
            transportPpq.store (*ppq, std::memory_order_relaxed);

        if (const auto signature = position.getTimeSignature(); signature && signature->denominator > 0)
            barLengthQuarters.store (4.0 * signature->numerator / signature->denominator, std::memory_order_relaxed);
    }

    void addAudioThreadTicks (juce::int64 ticks) noexcept;

    // HARD mode: should the power-down be engaged? And is an export running
    // (detected by speed, for hosts that don't flag it)? Wait-free reads.
    bool isUnplugRequested() const noexcept { return unplugTarget.load (std::memory_order_relaxed); }
    bool isExportDetected() const noexcept { return exportDetected.load (std::memory_order_relaxed); }

    //==============================================================================
    // Any non-realtime thread
    Snapshot getSnapshot() const;

    // For the history view: today's battery curve and the last 7 days' doses.
    struct History
    {
        std::vector<CurvePoint> today;
        int nowMinute = 0;
        std::vector<DayRecord> lastDays; // up to 7, oldest first, today last
        WeekSummary thisWeek, lastWeek;  // M10
    };

    History getHistory() const;

private:
    void run() override;

    void updateConfiguration();
    void updateRenderState (double wallSeconds);
    void updateHostState (double wallSeconds);
    void analyseAvailable();
    void summariseSecond (double wallSeconds);
    void updateModel (double wallSeconds, double offSeconds, double sleptSeconds);
    void advanceBreaks (const BreakInputs& inputs);
    void publishSnapshot();
    void loadState();
    void saveState();

    //==============================================================================
    // Shared with the audio thread
    SampleFifo fifo;
    std::atomic<const void*> active { nullptr };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<juce::int64> samplesProcessed { 0 }, samplesBypassed { 0 };
    std::atomic<bool> hostNonRealtimeSeen { false };
    std::atomic<juce::int64> audioTicksTotal { 0 }, audioBlocks { 0 }, audioTicksMax { 0 };
    std::atomic<bool> transportPlaying { false }, transportRecording { false };
    std::atomic<double> transportPpq { 0.0 }, barLengthQuarters { 4.0 };
    static_assert (std::atomic<double>::is_always_lock_free);

    // Message thread → worker
    std::mutex instancesMutex;
    std::vector<const void*> instances;
    std::atomic<int> setupSetting { (int) Setup::monitors };
    std::atomic<int> presetSetting { (int) LevelPreset::normal };
    std::atomic<int> strictnessSetting { (int) Strictness::escalating };
    std::atomic<int> timeWarp { 1 };
    std::atomic<bool> breakToggleRequested { false }, snoozeRequested { false }, notThatLoudRequested { false };
    std::atomic<int> monitorVolumeRequestDb { 0 };
    std::atomic<bool> unplugTarget { false }, exportDetected { false };

    // Worker thread only
    LoudnessMeter loudness;
    SpectrumAnalyser spectrum;
    GridLoopDetector loops;
    PlayheadTracker playhead;
    LevelCreepTracker creep;
    LoudnessWindow loudWindow { model::loudWindowSeconds };
    LoudnessWindow levelAWindow { model::loudWindowSeconds }; // the dose forecast
    double secondPresenceToBody = 0.0, secondHighsToBody = 0.0; // last second, unsmoothed
    double smoothedPresence = -std::numeric_limits<double>::infinity();
    double smoothedHighs = -std::numeric_limits<double>::infinity();
    double smoothedPsr = std::numeric_limits<double>::infinity();
    double preparedRate = 0.0;
    juce::int64 lastSamplesProcessed = 0;
    HostActivity host;
    SleepDetector sleepDetector;
    juce::int64 hostProcessedSeen = 0, hostBypassedSeen = 0;
    double secondOffSeconds = 0.0, secondSleptSeconds = 0.0; // collected until the next model tick
    double windowAudioSeconds = 0.0, windowWallSeconds = 0.0, windowSpeed = 0.0;
    double secondsSinceRenderEvidence = 0.0;
    bool rendering = false, currentRenderHadHostFlag = false;
    juce::int64 busyTicks = 0, lastAudioTicks = 0, lastAudioBlocks = 0;
    int wakes = 0;
    double maxWakeGapMs = 0.0;
    ExposureModel exposure;
    BreakController breaks;
    const juce::File stateFile;
    double secondsSinceSave = 0.0;
    int activeWarp = 1;
    Snapshot working;

    // Worker → UI / message thread
    mutable std::mutex snapshotMutex;
    Snapshot published;
    History publishedHistory;
    std::mutex alertsMutex;
    std::vector<Alert> pendingAlerts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Brain)
};

} // namespace breark
