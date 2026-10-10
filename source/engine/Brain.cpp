#include "Brain.h"

#include <algorithm>
#include <cmath>

namespace breark
{
namespace
{
// Background QoS wakes are coalesced by macOS (~150 ms measured instead of
// 50 ms), so the FIFO holds 2 s @ 48 kHz / 0.5 s @ 192 kHz (750 KB).
constexpr int fifoCapacityFrames = 96000;
constexpr int wakeIntervalMs = 50;
constexpr double saveIntervalSeconds = 60.0;

// Export detection: the host flag, or audio arriving faster than realtime.
constexpr double renderBurstFactor = 2.0;  // audio per wake > 2 × wall time …
constexpr double renderBurstSlack = 0.05;  // … + one large device buffer
constexpr double renderWindowSpeed = 1.3;  // or > 1.3 × realtime over a second
constexpr double renderHoldSeconds = 1.0;  // stay in render mode this long after the last evidence

float powerToDb (double power) noexcept
{
    return power > 0.0 ? (float) (10.0 * std::log10 (power)) : Snapshot::minusInfinity;
}
} // namespace

//==============================================================================
Brain::Brain (juce::File stateFileToUse)
    : juce::Thread ("brEARk analysis"),
      fifo (fifoCapacityFrames),
      stateFile (std::move (stateFileToUse))
{
    // Background QoS: macOS schedules it on efficiency cores and starves it
    // first under load. Live's audio threads are never affected.
    startThread (juce::Thread::Priority::background);
}

Brain::~Brain()
{
    stopThread (2000);
}

void Brain::attach (const void* instance)
{
    const std::scoped_lock lock (instancesMutex);
    instances.push_back (instance);

    if (active.load() == nullptr)
        active.store (instance, std::memory_order_release);
}

void Brain::detach (const void* instance)
{
    const std::scoped_lock lock (instancesMutex);
    instances.erase (std::remove (instances.begin(), instances.end(), instance), instances.end());

    if (active.load() == instance)
        active.store (instances.empty() ? nullptr : instances.front(), std::memory_order_release);
}

void Brain::prepare (double newSampleRate) noexcept
{
    sampleRate.store (newSampleRate);
}

void Brain::setSettings (Settings newSettings) noexcept
{
    setupSetting.store ((int) newSettings.setup);
    presetSetting.store ((int) newSettings.preset);
}

Settings Brain::getSettings() const noexcept
{
    return { (Setup) setupSetting.load(), (LevelPreset) presetSetting.load() };
}

void Brain::addAudioThreadTicks (juce::int64 ticks) noexcept
{
    audioTicksTotal.fetch_add (ticks, std::memory_order_relaxed);
    audioBlocks.fetch_add (1, std::memory_order_relaxed);

    if (ticks > audioTicksMax.load (std::memory_order_relaxed))
        audioTicksMax.store (ticks, std::memory_order_relaxed);
}

Snapshot Brain::getSnapshot() const
{
    const std::scoped_lock lock (snapshotMutex);
    return published;
}

//==============================================================================
void Brain::run()
{
    const juce::ScopedNoDenormals noDenormals;

    loadState();

    auto lastWakeMs = juce::Time::getMillisecondCounterHiRes();
    auto secondStartMs = lastWakeMs;

    while (! threadShouldExit())
    {
        wait (wakeIntervalMs);

        const auto busyStart = juce::Time::getHighResolutionTicks();
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();

        ++wakes;
        maxWakeGapMs = std::max (maxWakeGapMs, nowMs - lastWakeMs);

        updateConfiguration();
        updateRenderState ((nowMs - lastWakeMs) / 1000.0);
        lastWakeMs = nowMs;

        analyseAvailable();

        if (nowMs - secondStartMs >= 1000.0)
        {
            summariseSecond ((nowMs - secondStartMs) / 1000.0);
            updateModel ((nowMs - secondStartMs) / 1000.0);
            secondStartMs = nowMs;
        }

        publishSnapshot();
        busyTicks += juce::Time::getHighResolutionTicks() - busyStart;
    }

    saveState();
}

//==============================================================================
void Brain::loadState()
{
    const auto loaded = persistence::load (stateFile);
    const auto restored = loaded.value_or (PersistedState {});

    exposure = ExposureModel (restored.exposure);
    setSettings (restored.settings);
    exposure.setSettings (restored.settings);

    // The time Live was closed counts as silence: the ears rested.
    const auto now = juce::Time::getCurrentTime();
    const auto awaySeconds = loaded ? (double) (now.toMilliseconds() - restored.savedAtMs) / 1000.0 : 0.0;

    ListeningPeriod away;
    away.seconds = juce::jmax (0.0, awaySeconds);
    exposure.advance (away, now);
}

void Brain::saveState()
{
    if (activeWarp != 1) // time-warped state is a sandbox, never saved
        return;

    persistence::save (stateFile, { exposure.getSettings(), exposure.getState(), juce::Time::currentTimeMillis() });
    secondsSinceSave = 0.0;
}

void Brain::updateModel (double wallSeconds)
{
    // Entering time-warp: secure the real state. Leaving it: restore it.
    if (const auto warp = timeWarp.load(); warp != activeWarp)
    {
        if (activeWarp == 1)
            saveState();
        else
            loadState();

        activeWarp = warp;
    }

    exposure.setSettings (getSettings());

    const auto lufs = loudness.getShortTermLufs();

    ListeningPeriod period;
    period.seconds = wallSeconds * activeWarp;
    period.lufsShortTerm = lufs;
    period.levelA = working.levelA;
    period.silent = rendering || ! (lufs > model::silenceThresholdLufs); // exports never reach the ears
    exposure.advance (period, juce::Time::getCurrentTime());

    if ((secondsSinceSave += wallSeconds) >= saveIntervalSeconds)
        saveState();

    const auto& state = exposure.getState();
    working.battery = (float) state.battery;
    working.doseToday = (float) state.doseToday;
    working.doseWeek = (float) exposure.weeklyDose();
    working.perceivedSpl = period.silent ? Snapshot::minusInfinity : (float) exposure.perceivedSpl (lufs);
    working.doseSpl = period.silent ? Snapshot::minusInfinity : (float) exposure.doseSpl (working.levelA);
    working.referenceLufs = (float) state.referenceLufs;
    working.listeningSecondsToday = state.listeningSecondsToday;
    working.secondsSinceBreak = state.secondsSinceBreak;
    working.breaksToday = state.breaksToday;
    working.recovering = exposure.isRecovering();
    working.suggestedBreakMinutes = (float) exposure.suggestedBreakMinutes();
    working.settings = exposure.getSettings();
    working.timeWarp = activeWarp;
}

void Brain::updateConfiguration()
{
    const auto rate = sampleRate.load();

    if (rate <= 0.0 || juce::exactlyEqual (rate, preparedRate))
        return;

    loudness.prepare (rate);
    spectrum.prepare (rate);
    preparedRate = rate;
    lastSamplesProcessed = samplesProcessed.load();
    fifo.pull ([] (const float*, const float*, int) {}); // audio at the old rate is stale
}

void Brain::updateRenderState (double wallSeconds)
{
    if (preparedRate <= 0.0 || wallSeconds <= 0.0)
        return;

    const auto processed = samplesProcessed.load (std::memory_order_relaxed);
    const auto audioSeconds = (double) (processed - lastSamplesProcessed) / preparedRate;
    lastSamplesProcessed = processed;

    windowAudioSeconds += audioSeconds;
    windowWallSeconds += wallSeconds;

    if (windowWallSeconds >= 1.0)
    {
        windowSpeed = windowAudioSeconds / windowWallSeconds;
        windowAudioSeconds = windowWallSeconds = 0.0;
    }

    const auto hostFlag = hostNonRealtimeSeen.exchange (false, std::memory_order_relaxed);
    const auto burst = audioSeconds > renderBurstFactor * wallSeconds + renderBurstSlack;
    const auto evidence = hostFlag || burst || windowSpeed > renderWindowSpeed;

    if (evidence)
    {
        if (! rendering)
        {
            rendering = true;
            currentRenderHadHostFlag = false;
            ++working.rendersSeen;
        }

        currentRenderHadHostFlag = currentRenderHadHostFlag || hostFlag;
        secondsSinceRenderEvidence = 0.0;
    }
    else if (rendering && (secondsSinceRenderEvidence += wallSeconds) > renderHoldSeconds)
    {
        rendering = false;
        working.lastRenderHadHostFlag = currentRenderHadHostFlag;
    }
}

void Brain::analyseAvailable()
{
    const auto discard = rendering || preparedRate <= 0.0;

    fifo.pull ([this, discard] (const float* left, const float* right, int numFrames)
    {
        if (discard)
            return;

        loudness.process (left, right, numFrames);
        spectrum.process (left, right, numFrames);
    });
}

void Brain::summariseSecond (double wallSeconds)
{
    const auto totals = spectrum.takeTotals();

    working.levelA   = totals.frames > 0 ? powerToDb (totals.aWeighted / totals.frames) : Snapshot::minusInfinity;
    working.presence = totals.flat > 0.0 ? powerToDb (totals.presence / totals.flat) : Snapshot::minusInfinity;
    working.highs    = totals.flat > 0.0 ? powerToDb (totals.highs / totals.flat) : Snapshot::minusInfinity;
    working.framesAnalysed += totals.frames;

    const auto ticksPerSecond = (double) juce::Time::getHighResolutionTicksPerSecond();
    working.workerLoadPercent = (float) (100.0 * (double) busyTicks / (wallSeconds * ticksPerSecond));
    working.workerWakeMsAverage = wakes > 0 ? (float) (1000.0 * wallSeconds / wakes) : 0.0f;
    working.workerWakeMsMax = (float) maxWakeGapMs;
    busyTicks = 0;
    wakes = 0;
    maxWakeGapMs = 0.0;

    const auto ticks = audioTicksTotal.load (std::memory_order_relaxed);
    const auto blocks = audioBlocks.load (std::memory_order_relaxed);

    working.audioMicrosAverage = blocks > lastAudioBlocks
                                   ? (float) (1.0e6 * (double) (ticks - lastAudioTicks) / (double) (blocks - lastAudioBlocks) / ticksPerSecond)
                                   : 0.0f;
    working.audioMicrosMax = (float) (1.0e6 * (double) audioTicksMax.exchange (0, std::memory_order_relaxed) / ticksPerSecond);
    lastAudioTicks = ticks;
    lastAudioBlocks = blocks;

    working.renderSpeed = (float) windowSpeed;
}

void Brain::publishSnapshot()
{
    working.lufsMomentary = loudness.getMomentaryLufs();
    working.lufsShortTerm = loudness.getShortTermLufs();
    working.peakShortTerm = loudness.getShortTermPeakDb();
    working.silent = ! (working.lufsShortTerm > model::silenceThresholdLufs);
    working.sampleRate = preparedRate;
    working.fftSize = spectrum.getFftSize();
    working.rendering = rendering;
    working.samplesDropped = fifo.getDroppedSamples();

    const std::scoped_lock lock (snapshotMutex);
    published = working;
}

} // namespace breark
