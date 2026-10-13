#include "Brain.h"

#include <algorithm>
#include <cmath>
#include <utility>

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
    strictnessSetting.store ((int) newSettings.strictness);
}

Settings Brain::getSettings() const noexcept
{
    return { (Setup) setupSetting.load(), (LevelPreset) presetSetting.load(), (Strictness) strictnessSetting.load() };
}

std::vector<Alert> Brain::takeAlerts()
{
    const std::scoped_lock lock (alertsMutex);
    return std::exchange (pendingAlerts, {});
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

Brain::History Brain::getHistory() const
{
    const std::scoped_lock lock (snapshotMutex);
    return publishedHistory;
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
        const auto wakeSeconds = (nowMs - lastWakeMs) / 1000.0;

        ++wakes;
        maxWakeGapMs = std::max (maxWakeGapMs, nowMs - lastWakeMs);
        secondSleptSeconds += sleepDetector.update (nowMs, juce::Time::currentTimeMillis());

        updateConfiguration();
        updateRenderState (wakeSeconds);
        updateHostState (wakeSeconds);
        lastWakeMs = nowMs;

        analyseAvailable();

        PlayheadTracker::Transport transport;
        transport.playing = transportPlaying.load (std::memory_order_relaxed) && ! rendering
                         && host.getState() == HostState::playing;
        transport.ppq = transportPpq.load (std::memory_order_relaxed);
        transport.barLengthQuarters = barLengthQuarters.load (std::memory_order_relaxed);
        playhead.update (transport, nowMs / 1000.0);

        if (nowMs - secondStartMs >= 1000.0)
        {
            summariseSecond ((nowMs - secondStartMs) / 1000.0);
            updateModel ((nowMs - secondStartMs) / 1000.0, std::exchange (secondOffSeconds, 0.0), std::exchange (secondSleptSeconds, 0.0));
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

void Brain::updateModel (double wallSeconds, double offSeconds, double sleptSeconds)
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
    const auto now = juce::Time::getCurrentTime();

    // The Mac slept: the ears rested, as when Live is closed.
    if (sleptSeconds > 0.0)
    {
        ListeningPeriod asleep; // silent
        asleep.seconds = sleptSeconds;
        exposure.advance (asleep, now);

        BreakInputs restInputs; // silent
        restInputs.seconds = sleptSeconds;
        restInputs.battery = exposure.getState().battery;
        restInputs.suggestedBreakMinutes = exposure.suggestedBreakMinutes();
        restInputs.strictness = exposure.getSettings().strictness;
        advanceBreaks (restInputs);
    }

    // Switched off in Live: that time is frozen (nothing drains, recovers or
    // escalates) and only counted for the history. Settings and buttons still
    // apply, in zero time.
    // (Both come from the same clock but are summed differently: a rounding
    // remainder of an off second must not count as time switched on.)
    offSeconds = std::min (offSeconds, wallSeconds);
    const auto frozen = wallSeconds - offSeconds < 0.001;
    const auto onSeconds = frozen ? 0.0 : wallSeconds - offSeconds;
    exposure.addOffTime (offSeconds * activeWarp, now);

    const auto lufs = loudness.getShortTermLufs();
    // Exports never reach the ears, and neither does sound while unplugged.
    const auto muted = breaks.getPhase() == BreakPhase::unplugged;
    const auto silent = frozen || rendering || muted || ! (lufs > model::silenceThresholdLufs);

    // Factor inputs, averaged over ~10 s of playback.
    auto smooth = [onSeconds] (double& smoothed, double value)
    {
        if (! std::isfinite (value))
            return;

        smoothed = std::isfinite (smoothed) ? smoothed + (value - smoothed) * std::min (1.0, onSeconds / model::factorSmoothingSeconds)
                                            : value;
    };

    if (! silent)
    {
        smooth (smoothedPresence, secondPresenceToBody);
        smooth (smoothedHighs, secondHighsToBody);
        smooth (smoothedPsr, (double) loudness.getShortTermPeakDb() - lufs);
    }

    if (! frozen)
    {
        loops.update (onSeconds, silent);
        creep.add (lufs, onSeconds, silent);
        // The windows only count playing time: a pause neither dilutes them
        // nor ends them; only a real break (5 min of silence) starts them over.
        if (! silent)
        {
            loudWindow.add (lufs, onSeconds);
            levelAWindow.add (working.levelA, onSeconds);
            nowWindow.add (lufs, onSeconds);
        }
        else if (exposure.getState().silenceSeconds >= model::breakMinimumSeconds)
        {
            loudWindow.reset();
            levelAWindow.reset();
            nowWindow.reset();
        }
    }

    // A level click or a check-in answer (M11): what plays now (the last 30 s
    // of play, or the last 3 s at first) is that level on your ears.
    if (const auto requested = levelRequest.exchange (-1); requested >= 0)
    {
        const auto playingNow = std::isfinite (nowWindow.getLufs()) ? nowWindow.getLufs() : lufs;

        if (! silent && std::isfinite (playingNow) && playingNow > model::silenceThresholdLufs)
            exposure.anchorLevel (playingNow);

        checkIn.answered();
    }

    FactorInputs inputs;
    inputs.presenceToBodyDb = smoothedPresence;
    inputs.highsToBodyDb = smoothedHighs;
    inputs.psrDb = smoothedPsr;
    // Loop strain from the audio (plays of the detected loop) or the playhead
    // (plays of the current bar, the loop's length if known), whichever is higher.
    const auto barPlays = playhead.getCurrentPlays (juce::Time::getMillisecondCounterHiRes() / 1000.0);
    const auto loopSeconds = loops.getLoopSeconds();
    const auto audioPlays = loopSeconds > 0.0 ? loops.getLoopingSeconds() / loopSeconds : 0.0;
    inputs.loopStrainMinutes = std::max (loopStrainMinutes (audioPlays, loopSeconds),
                                         loopStrainMinutes (barPlays, loops.getBarSeconds() * std::max (1, loops.getLoopBars())));
    inputs.localHour = now.getHours() + now.getMinutes() / 60.0;

    const auto factors = silent ? FatigueFactors {} : computeFatigueFactors (inputs);

    ListeningPeriod period;
    period.seconds = onSeconds * activeWarp;
    period.lufsShortTerm = lufs;
    period.levelA = working.levelA;
    period.silent = silent;
    period.fatigueFactor = factors.product();
    period.onBreak = breaks.getPhase() == BreakPhase::onBreak;
    exposure.advance (period, now);

    // Breaks and alerts.
    if (breakToggleRequested.exchange (false))
        breaks.toggleBreak (exposure.suggestedBreakMinutes());

    if (snoozeRequested.exchange (false))
        breaks.snooze();

    BreakInputs breakInputs;
    breakInputs.seconds = period.seconds;
    breakInputs.battery = exposure.getState().battery;
    breakInputs.silent = silent;
    breakInputs.recording = transportRecording.load (std::memory_order_relaxed);
    breakInputs.suggestedBreakMinutes = exposure.suggestedBreakMinutes();
    breakInputs.strictness = exposure.getSettings().strictness;
    breakInputs.doseFull = exposure.getState().doseToday >= 100.0;
    advanceBreaks (breakInputs);

    // Check-ins (M11): "How loud is it on your ears now?"
    if (! frozen)
    {
        const auto recent = loudWindow.getLufs();
        const auto reason = checkIn.update (onSeconds, ! silent, recent, exposure.getState().referenceLufs);

        if (reason != CheckInPolicy::Reason::none)
        {
            Alert ask;
            ask.kind = AlertKind::checkIn;
            ask.checkIn = (int) reason;
            ask.deviationLu = std::isfinite (recent) ? recent - exposure.getState().referenceLufs : 0.0;
            const std::scoped_lock lock (alertsMutex);
            pendingAlerts.push_back (ask);
        }
    }

    // The weekly summary, once a week, the first time you listen in a new week.
    if (const auto week = ExposureModel::weekNumber (exposure.getState().day);
        ! silent && activeWarp == 1 && exposure.getState().lastSummaryWeek < week)
    {
        exposure.setLastSummaryWeek (week);

        if (const auto lastWeek = exposure.summariseWeek (week - 1); lastWeek.daysListened > 0)
        {
            Alert summary;
            summary.kind = AlertKind::weeklySummary;
            summary.week = lastWeek;
            const std::scoped_lock lock (alertsMutex);
            pendingAlerts.push_back (summary);
        }
    }

    unplugTarget.store (breaks.isUnplugRequested(), std::memory_order_relaxed);

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
    working.offSecondsToday = state.offSecondsToday;
    working.levelAnchored = state.levelAnchored;
    {
        const auto recent = std::isfinite (loudWindow.getLufs()) ? loudWindow.getLufs() : lufs;
        working.signalVsAnchorLu = silent || ! std::isfinite (recent) ? 0.0f : (float) (recent - state.referenceLufs);
    }
    working.dayDone = state.doseToday >= 100.0;
    working.unpluggedForDay = breaks.isUnpluggedForTheDay();
    working.longestStretchToday = state.longestStretchToday;
    working.emptyBatteryToday = state.emptyBatteryToday;

    // Forecasts at the level of the last 2 minutes (or what plays now, at first).
    {
        const auto recentLufs = std::isfinite (loudWindow.getLufs()) ? loudWindow.getLufs() : lufs;
        const auto recentLevelA = std::isfinite (levelAWindow.getLufs()) ? levelAWindow.getLufs() : working.levelA;
        const auto playing = ! silent;
        const auto fatigue = factors.product();

        working.minutesToBreak = playing ? exposure.minutesToEmpty (recentLufs, fatigue) : std::numeric_limits<double>::infinity();
        working.minutesToBreakQuieter = playing && exposure.stepDownDb() < 0.0 ? exposure.minutesToEmpty (recentLufs, fatigue, exposure.stepDownDb())
                                                                               : std::numeric_limits<double>::infinity();
        working.minutesToFullDose = playing ? exposure.minutesToFullDose (recentLevelA) : std::numeric_limits<double>::infinity();
        working.minutesToRecover = exposure.minutesToRecover (model::suggestedBreakTargetBattery);
    }
    working.recovering = exposure.isRecovering();
    working.suggestedBreakMinutes = (float) exposure.suggestedBreakMinutes();
    working.settings = exposure.getSettings();
    working.timeWarp = activeWarp;

    working.presenceToBody = (float) smoothedPresence;
    working.highsToBody = (float) smoothedHighs;
    working.psr = (float) smoothedPsr;
    working.factors = factors;
    working.drainFactor = silent ? 1.0f : (float) exposure.combinedFactor (factors.product());
    working.barPlays = (float) barPlays;
    working.loopingSeconds = loops.getLoopingSeconds();
    working.loopBars = loops.getLoopBars();
    working.loopSeconds = loopSeconds;
    working.loopStrain = (float) inputs.loopStrainMinutes;
    working.loopBestRhythm = loops.getBestRhythm();
    working.loopBestShiftDb = loops.getBestShiftDb();
    working.loopBestBars = loops.getBestBars();
    working.loopHasGrid = loops.hasGrid();
    working.levelCreepDb = (float) creep.getCreepDb();
    working.breakPhase = breaks.getPhase();
    working.unpluggedSeconds = breaks.getUnpluggedSeconds();
    working.snoozeSecondsLeft = breaks.getSnoozeSecondsLeft();
    working.powerDownsToday = exposure.getState().powerDownsToday;
    working.snoozesToday = exposure.getState().snoozesToday;
    working.secondsSinceDue = breaks.getSecondsSinceDue();
    working.breakSecondsLeft = breaks.getBreakSecondsLeft();
    working.transportPlaying = transportPlaying.load (std::memory_order_relaxed);

    // History, once per model tick (cheap: at most 1440 points).
    History history;
    history.today = state.todayCurve;
    history.nowMinute = ExposureModel::minuteOfDay (now);

    for (const auto& record : state.history)
        if (record.day > state.day - 7)
            history.lastDays.push_back (record);

    history.lastDays.push_back (exposure.todayRecord());

    const auto week = ExposureModel::weekNumber (state.day);
    history.thisWeek = exposure.summariseWeek (week);
    history.lastWeek = exposure.summariseWeek (week - 1);

    const std::scoped_lock lock (snapshotMutex);
    publishedHistory = std::move (history);
    working.transportRecording = transportRecording.load (std::memory_order_relaxed);
}

void Brain::advanceBreaks (const BreakInputs& inputs)
{
    auto alerts = breaks.advance (inputs);

    if (alerts.empty())
        return;

    for (const auto& alert : alerts)
    {
        if (alert.kind == AlertKind::unplug || alert.kind == AlertKind::dayUnplug) exposure.countPowerDown();
        if (alert.kind == AlertKind::snoozed) exposure.countSnooze();
        if (alert.kind == AlertKind::breakDue || alert.kind == AlertKind::unplug) exposure.countEmptyBattery();
    }

    const std::scoped_lock lock (alertsMutex);
    pendingAlerts.insert (pendingAlerts.end(), alerts.begin(), alerts.end());
}

void Brain::updateHostState (double wallSeconds)
{
    const auto processed = samplesProcessed.load (std::memory_order_relaxed);
    const auto bypassed = samplesBypassed.load (std::memory_order_relaxed);
    const auto before = host.getState();
    const auto state = host.update (processed - hostProcessedSeen, bypassed - hostBypassedSeen, wallSeconds);
    hostProcessedSeen = processed;
    hostBypassedSeen = bypassed;

    // The last audio must neither keep counting after it stopped nor linger
    // when brEARk is switched back on.
    if (state != before && state != HostState::playing)
        loudness.reset();

    if (state == HostState::off)
        secondOffSeconds += wallSeconds;
}

void Brain::updateConfiguration()
{
    const auto rate = sampleRate.load();

    if (rate <= 0.0 || juce::exactlyEqual (rate, preparedRate))
        return;

    loudness.prepare (rate);
    spectrum.prepare (rate);
    loops.prepare (rate);
    preparedRate = rate;
    lastSamplesProcessed = samplesProcessed.load();

    // Audio at the old rate is stale.
    fifo.pullStamps ([] (const BlockStamp&) {});
    fifo.pull ([] (const float*, const float*, int, juce::int64) {});
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

    exportDetected.store (rendering, std::memory_order_relaxed);
}

void Brain::analyseAvailable()
{
    const auto discard = rendering || preparedRate <= 0.0;

    // Stamps first: they always precede the audio they describe.
    fifo.pullStamps ([this, discard] (const BlockStamp& stamp)
    {
        if (! discard)
            loops.addStamp (stamp);
    });

    fifo.pull ([this, discard] (const float* left, const float* right, int numFrames, juce::int64 streamPosition)
    {
        if (discard)
            return;

        loudness.process (left, right, numFrames);
        spectrum.process (left, right, numFrames);
        loops.addAudio (left, right, numFrames, streamPosition);
    });
}

void Brain::summariseSecond (double wallSeconds)
{
    const auto totals = spectrum.takeTotals();

    working.levelA   = totals.frames > 0 ? powerToDb (totals.aWeighted / totals.frames) : Snapshot::minusInfinity;
    secondPresenceToBody = totals.body > 0.0 ? (double) powerToDb (totals.presence / totals.body) : (double) Snapshot::minusInfinity;
    secondHighsToBody    = totals.body > 0.0 ? (double) powerToDb (totals.highs / totals.body) : (double) Snapshot::minusInfinity;
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
    working.hostOff = host.getState() == HostState::off;
    working.audioStalled = host.getState() == HostState::stalled;
    working.sampleRate = preparedRate;
    working.fftSize = spectrum.getFftSize();
    working.rendering = rendering;
    working.samplesDropped = fifo.getDroppedSamples();

    const std::scoped_lock lock (snapshotMutex);
    published = working;
}

} // namespace breark
