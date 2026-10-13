// brEARk soak test
// ----------------
// Runs brEARk (built in: the same code as the plugin) for hours in real time,
// the way Live would: music through a paced realtime audio thread with a
// playhead, while the message thread opens and closes the editor. Each cycle
// (~8 min) changes the "audio device" (sample rate, block size, 32/64-bit,
// mono/stereo), plays, goes silent, switches brEARk off and on, stops the
// audio, and adds a second instance and removes the first. The first cycle and
// then one cycle per hour adds a time-warped HARD run that pulls the plug and
// plugs it back in.
//
// Checks: output bit-identical to the input (except while HARD mode is
// working), no allocation, lock, wait or I/O on the audio thread inside brEARk
// (RealtimeCheck library), no dropped samples, memory flat, every expected
// state seen. Writes soak.csv (one row per 10 s). Pop-ups and sounds are off.
//
// Usage: brEARk_Soak [minutes = 180] [output folder = current folder] [quick]
//        brEARk_Soak showcase [folder]: the README screenshots (a scripted session, 2.5 min)
//        "quick" shortens every phase to a tenth: a smoke test of the soak itself.
// Exit code 0 = all checks passed.

#include "PluginProcessor.h"
#include "../tests/RealisticMusic.h"

#include <dlfcn.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>

using namespace breark;

namespace
{
//==============================================================================
// The realtime check library, if loaded (see main()).
struct RealtimeCheck
{
    void (*watch) (bool) = nullptr;
    long (*violations)() = nullptr;
    void (*reset)() = nullptr;
    void (*print)() = nullptr;

    RealtimeCheck()
    {
        watch = (void (*) (bool)) dlsym (RTLD_DEFAULT, "breark_rtcheck_watch");
        violations = (long (*)()) dlsym (RTLD_DEFAULT, "breark_rtcheck_violations");
        reset = (void (*)()) dlsym (RTLD_DEFAULT, "breark_rtcheck_reset");
        print = (void (*)()) dlsym (RTLD_DEFAULT, "breark_rtcheck_print");
    }

    bool loaded() const noexcept { return watch != nullptr && violations != nullptr && reset != nullptr && print != nullptr; }
};

RealtimeCheck rtcheck;

double footprintMB()
{
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &info, &count);
    return (double) info.phys_footprint / 1048576.0;
}

double mallocInUseMB()
{
    malloc_statistics_t stats {};
    malloc_zone_statistics (nullptr, &stats);
    return (double) stats.size_in_use / 1048576.0;
}

//==============================================================================
struct DeviceConfig
{
    double rate;
    int block;
    bool doublePrecision;
    int channels;

    juce::String describe() const
    {
        return juce::String (rate, 0) + " Hz / " + juce::String (block) + " / " + (doublePrecision ? "64-bit" : "32-bit")
             + " / " + (channels == 2 ? "stereo" : "mono");
    }
};

enum class Mode { play, silence, bypass, stall };

// A transport at 124 bpm whose position follows the audio.
class Transport final : public juce::AudioPlayHead
{
public:
    std::atomic<bool> playing { false };
    double rate = 48000.0;      // audio thread (set while stopped)
    juce::int64 position = 0;   // audio thread

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (124.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (playing.load());
        info.setTimeInSamples (position);
        info.setPpqPosition ((double) position / rate * 124.0 / 60.0);
        return info;
    }
};

//==============================================================================
// The "audio device": a realtime thread processing every instance in turn,
// paced to the wall clock like an audio callback.
class AudioDevice final : public juce::Thread
{
public:
    AudioDevice() : juce::Thread ("soak audio") {}

    ~AudioDevice() override { stop(); }

    // Message thread, device stopped.
    void configure (const DeviceConfig& newConfig, const std::vector<float>& newMusic, std::vector<BrearkProcessor*> newInstances)
    {
        jassert (! isThreadRunning());
        config = newConfig;
        music = &newMusic;
        instances = std::move (newInstances);
        transport.rate = config.rate;

        input.setSize (config.channels, config.block);
        work.setSize (config.channels, config.block);
        inputDouble.setSize (config.channels, config.block);
        workDouble.setSize (config.channels, config.block);

        for (auto* p : instances)
            p->setPlayHead (&transport);
    }

    void start()
    {
        startRealtimeThread (juce::Thread::RealtimeOptions {}.withApproximateAudioProcessingTime (config.block, config.rate));
    }

    void stop() { stopThread (2000); }

    std::atomic<Mode> mode { Mode::play };
    std::atomic<bool> hardPhase { false }; // the power-down may change the audio now
    std::atomic<juce::int64> blocks { 0 }, samples { 0 }, mismatches { 0 }, mismatchesOutsideHard { 0 };
    Transport transport;

private:
    void run() override
    {
        const auto blockMs = 1000.0 * config.block / config.rate;
        auto nextMs = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            const auto currentMode = mode.load();

            if (currentMode != Mode::stall)
            {
                transport.playing = currentMode == Mode::play || currentMode == Mode::bypass;

                if (config.doublePrecision)
                    processBlock (inputDouble, workDouble, currentMode);
                else
                    processBlock (input, work, currentMode);

                ++blocks;
                samples += config.block;
            }

            nextMs += blockMs;
            const auto waitMs = nextMs - juce::Time::getMillisecondCounterHiRes();

            if (waitMs > 1.0)
                juce::Thread::sleep ((int) waitMs);
            else if (waitMs < -500.0)
                nextMs = juce::Time::getMillisecondCounterHiRes(); // never catch up in a burst
        }
    }

    template <typename SampleType>
    void processBlock (juce::AudioBuffer<SampleType>& in, juce::AudioBuffer<SampleType>& out, Mode currentMode)
    {
        const auto n = config.block;
        const auto& source = *music;

        for (int i = 0; i < n; ++i)
        {
            const auto v = currentMode == Mode::silence ? 0.0f : source[(size_t) ((musicPosition + i) % (juce::int64) source.size())];

            for (int ch = 0; ch < config.channels; ++ch)
                in.setSample (ch, i, (SampleType) (ch == 0 ? v : 0.9f * v));
        }

        musicPosition += n;
        transport.position += n;

        for (int ch = 0; ch < config.channels; ++ch)
            out.copyFrom (ch, 0, in, ch, 0, n);

        for (auto* p : instances)
        {
            if (rtcheck.loaded()) rtcheck.watch (true);

            if (currentMode == Mode::bypass)
                p->processBlockBypassed (out, midi);
            else
                p->processBlock (out, midi);

            if (rtcheck.loaded()) rtcheck.watch (false);
        }

        for (int ch = 0; ch < config.channels; ++ch)
        {
            if (std::memcmp (out.getReadPointer (ch), in.getReadPointer (ch), sizeof (SampleType) * (size_t) n) != 0)
            {
                ++mismatches;

                if (! hardPhase.load())
                    ++mismatchesOutsideHard;

                break;
            }
        }
    }

    DeviceConfig config { 48000.0, 512, false, 2 };
    const std::vector<float>* music = nullptr;
    std::vector<BrearkProcessor*> instances;
    juce::AudioBuffer<float> input, work;
    juce::AudioBuffer<double> inputDouble, workDouble;
    juce::MidiBuffer midi;
    juce::int64 musicPosition = 0;
};

//==============================================================================
// Runs the message loop (timers, editor) for a while.
void pump (double ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) ms);
}

// Clicks the first visible button with this text, anywhere in the editor.
bool clickButton (juce::Component& parent, const juce::String& text)
{
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible())
            continue;

        if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == text && button->onClick)
        {
            button->onClick();
            return true;
        }

        if (clickButton (*child, text))
            return true;
    }

    return false;
}

const char* stageName (PowerDown::Stage s)
{
    switch (s)
    {
        case PowerDown::Stage::run:          return "run";
        case PowerDown::Stage::spinningDown: return "spin-down";
        case PowerDown::Stage::stopped:      return "stopped";
        case PowerDown::Stage::spinningUp:   return "spin-up";
    }

    return "";
}

// Re-launches this program with the realtime check library inserted.
void insertRealtimeCheck (char* argv[])
{
    if (rtcheck.loaded() || std::getenv ("BREARK_SOAK_NO_RTCHECK") != nullptr)
        return;

    const auto library = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                             .getSiblingFile ("libbrEARk_RealtimeCheck.dylib");

    if (! library.existsAsFile())
        return;

    setenv ("DYLD_INSERT_LIBRARIES", library.getFullPathName().toRawUTF8(), 1);
    execv (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName().toRawUTF8(), argv);
}
} // namespace

//==============================================================================
// A scripted session for the README screenshots: a realistic day so far
// is loaded (two hours in, one break, a week of history, the level set to
// NORMAL a while ago), then brEARk listens in real time to a mastered house
// loop for 2.5 minutes. Everything on screen is measured or computed by the
// running plugin; only the past (the loaded state) is scripted.
static int runShowcase (const juce::File& folder)
{
    const auto stateFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("brEARk-showcase-state", ".json");
    setenv ("BREARK_STATE_FILE", stateFile.getFullPathName().toRawUTF8(), 1);
    setenv ("BREARK_QUIET", "1", 1);
    const juce::ScopeGuard removeStateFile { [&] { stateFile.deleteFile(); } };

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    folder.createDirectory();

    // The music: 8 bars of house at 48 kHz, soft-limited like a finished master.
    test::HouseOptions options;
    options.sampleRate = 48000.0;
    options.hatJitterDb = 1.5;
    options.padLfoDb = 1.0;
    juce::Random human (7);
    auto music = test::renderHouse (8, [] (int bar) { return bar % 4; }, options, human);

    auto peak = 0.0f;
    for (const auto x : music) peak = std::max (peak, std::abs (x));
    for (auto& x : music) x = 0.89f * std::tanh (4.0f * x / peak) / std::tanh (4.0f);

    // Its loudness, as the audio device will play it (left, right at 90 %).
    std::vector<float> right (music.size());
    for (size_t i = 0; i < music.size(); ++i) right[i] = 0.9f * music[i];
    LoudnessMeter meter;
    meter.prepare (48000.0);
    meter.process (music.data(), right.data(), (int) music.size());
    const auto musicLufs = (double) meter.getShortTermLufs();

    // The day so far.
    const auto now = juce::Time::getCurrentTime();
    const auto today = ExposureModel::dayNumber (now);
    const auto nowMinute = ExposureModel::minuteOfDay (now);

    PersistedState state;
    state.savedAtMs = juce::Time::currentTimeMillis();
    state.settings = { Setup::monitors, LevelPreset::normal, Strictness::escalating };

    auto& e = state.exposure;
    e.day = today;
    e.battery = 52.0;
    e.doseToday = 38.0;
    e.listeningSecondsToday = 2.2 * 3600.0;
    e.secondsSinceBreak = 51.0 * 60.0;
    e.breaksToday = 1;
    e.longestStretchToday = 75.0 * 60.0;
    e.referenceLufs = musicLufs - 3.0; // set NORMAL on a slightly quieter track
    e.levelAnchored = true;
    e.referencePlaybackSeconds = 6.0 * 3600.0;
    e.lastSummaryWeek = ExposureModel::weekNumber (today);

    // Today's curve: 75 min of work, a 15 min break, an hour since.
    for (int t = 0; t < 156; ++t)
    {
        const auto minute = nowMinute - 156 + t;

        if (minute < 0)
            continue;

        const auto battery = t < 75 ? 100.0 - 0.56 * t
                           : t < 90 ? 100.0 - 42.0 * std::exp (-(t - 75) / 5.0)
                                    : 97.9 - 0.69 * (t - 90);
        e.todayCurve.push_back ({ minute, juce::roundToInt (battery), false });
    }

    const double doses[] = { 34.0, 61.0, 0.0, 48.0, 112.0, 22.0 };
    for (int d = 0; d < 6; ++d)
        if (doses[d] > 0.0)
            e.history.push_back ({ today - 6 + d, doses[d], doses[d] / 100.0 * 4.5 * 3600.0, 2 + d % 3, 0, 0, 0.0, (55.0 + 10.0 * d) * 60.0, d == 4 ? 2 : 0 });

    persistence::save (stateFile, state);

    // brEARk on the master, listening in real time.
    auto processor = std::make_unique<BrearkProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    processor->setBusesLayout (layout);
    processor->prepareToPlay (48000.0, 512);

    AudioDevice device;
    device.configure ({ 48000.0, 512, false, 2 }, music, { processor.get() });
    device.mode = Mode::play;
    device.start();

    std::cout << "brEARk showcase: listening for 150 s (" << juce::String (musicLufs, 1) << " LUFS) ...\n";
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    while (juce::Time::getMillisecondCounterHiRes() - startMs < 150000.0)
        pump (100.0);

    auto save = [&] (juce::Component& c, const juce::String& name)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        const auto file = folder.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::cout << "  " << file.getFullPathName() << "\n";
    };

    for (const auto poster : { true, false })
    {
        processor->setPosterSize (poster);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
        pump (600.0);
        save (*editor, poster ? "showcase-poster.png" : "showcase-compact.png");

        if (poster && clickButton (*editor, "HISTORY"))
        {
            pump (300.0);
            save (*editor, "showcase-history.png");
        }
    }

    device.stop();
    processor.reset();
    return 0;
}

//==============================================================================
int main (int argc, char* argv[])
{
    insertRealtimeCheck (argv);

    if (argc > 1 && juce::String (argv[1]) == "showcase")
        return runShowcase (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "."));

    const auto minutes = argc > 1 ? juce::jmax (1, juce::String (argv[1]).getIntValue()) : 180;
    const juce::File folder (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "."));
    const auto scale = argc > 3 && juce::String (argv[3]) == "quick" ? 0.1 : 1.0; // phase lengths
    folder.createDirectory();

    // The hosted brEARk must not touch the real listening history, nor pop up or play anything.
    const auto stateFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("brEARk-soak-state", ".json");
    setenv ("BREARK_STATE_FILE", stateFile.getFullPathName().toRawUTF8(), 1);
    setenv ("BREARK_QUIET", "1", 1);
    const juce::ScopeGuard removeStateFile { [&] { stateFile.deleteFile(); } };

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::StringArray failures;

    std::cout << "brEARk soak test: " << minutes << " min, output in " << folder.getFullPathName() << "\n";

    // The realtime check must demonstrably catch an allocation before it can vouch for anything.
    if (! rtcheck.loaded())
    {
        failures.add ("realtime check library not loaded");
    }
    else
    {
        // volatile: the compiler may not optimise the allocation away.
        static void* volatile probe = nullptr;
        rtcheck.watch (true);
        probe = std::malloc (64);
        rtcheck.watch (false);
        std::free (probe);

        const auto caught = rtcheck.violations();
        std::cout << "  realtime check: " << (caught == 1 ? juce::String ("active (self-test caught 1 allocation)")
                                                          : "BROKEN (self-test caught " + juce::String (caught) + ")") << "\n";

        if (caught != 1)
            failures.add ("realtime check self-test failed");

        rtcheck.reset();
    }

    // Music: 8 bars of house per sample rate (a loop, like Live's loop brace).
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    std::map<double, std::vector<float>> music;

    for (const auto rate : rates)
    {
        test::HouseOptions options;
        options.sampleRate = rate;
        options.hatJitterDb = 1.5;
        options.padLfoDb = 1.0;
        juce::Random human (7);
        music[rate] = test::renderHouse (8, [] (int bar) { return bar % 4; }, options, human);
    }

    const std::vector<DeviceConfig> configs {
        { 48000.0, 512, false, 2 }, { 44100.0, 128, true, 2 }, { 96000.0, 1024, false, 2 }, { 192000.0, 2048, true, 2 },
        { 48000.0, 64, true, 1 },   { 44100.0, 2048, false, 2 }, { 96000.0, 256, true, 2 },  { 192000.0, 512, false, 1 },
    };

    const auto baselineMB = footprintMB();
    const auto baselineMallocMB = mallocInUseMB();

    std::vector<std::unique_ptr<BrearkProcessor>> processors;
    processors.push_back (std::make_unique<BrearkProcessor>());
    AudioDevice device;

    auto pointers = [&]
    {
        std::vector<BrearkProcessor*> result;
        for (auto& p : processors) result.push_back (p.get());
        return result;
    };

    auto applyConfig = [&] (const DeviceConfig& config)
    {
        device.stop();

        for (auto& p : processors)
        {
            p->releaseResources();
            juce::AudioProcessor::BusesLayout layout;
            const auto set = config.channels == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
            layout.inputBuses.add (set);
            layout.outputBuses.add (set);
            p->setBusesLayout (layout);
            p->setProcessingPrecision (config.doublePrecision ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
            p->prepareToPlay (config.rate, config.block);
        }

        device.configure (config, music[config.rate], pointers());
        device.start();
    };

    // CSV, one row per 10 s.
    auto csv = folder.getChildFile ("soak.csv");
    csv.replaceWithText ("seconds,cycle,phase,config,instances,footprint_mb,malloc_mb,battery,dose,off_s,host_off,stalled,silent,lufs_s,"
                         "worker_pct,wake_ms_max,audio_us_avg,audio_us_max,dropped,rt_violations,mismatches,power\n");

    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    auto elapsedSeconds = [&] { return (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0; };
    auto nextRowAt = 0.0;
    auto cycle = 0;
    juce::String phase, configName;
    std::vector<double> rowsFootprint, rowsMalloc;           // this cycle's rows
    std::vector<double> cycleFootprint, cycleMalloc;         // medians per finished cycle
    double engineMB = 0.0, editorMB = 0.0, maxAudioMicros = 0.0, maxWorkerPercent = 0.0;
    auto sawOff = false, sawStall = false, sawTakeover = true, sawPowerDown = false, sawPowerBack = false;
    juce::int64 maxDropped = 0;

    auto active = [&]() -> BrearkProcessor& { return *processors.front(); };

    auto writeRow = [&]
    {
        const auto s = active().getBrain().getSnapshot();
        maxAudioMicros = std::max (maxAudioMicros, (double) s.audioMicrosMax);
        maxWorkerPercent = std::max (maxWorkerPercent, (double) s.workerLoadPercent);
        maxDropped = std::max (maxDropped, s.samplesDropped);

        juce::StringArray row {
            juce::String (elapsedSeconds(), 0), juce::String (cycle), phase, configName.quoted(), juce::String ((int) processors.size()),
            juce::String (footprintMB(), 2), juce::String (mallocInUseMB(), 2), juce::String (s.battery, 1), juce::String (s.doseToday, 2),
            juce::String (s.offSecondsToday, 0), juce::String ((int) s.hostOff), juce::String ((int) s.audioStalled), juce::String ((int) s.silent),
            juce::String (s.lufsShortTerm, 1), juce::String (s.workerLoadPercent, 3), juce::String (s.workerWakeMsMax, 0),
            juce::String (s.audioMicrosAverage, 2), juce::String (s.audioMicrosMax, 2), juce::String (s.samplesDropped),
            juce::String (rtcheck.loaded() ? rtcheck.violations() : -1), juce::String (device.mismatchesOutsideHard.load()),
            stageName (active().getPowerStage())
        };

        csv.appendText (row.joinIntoString (",") + "\n");
        rowsFootprint.push_back (row[5].getDoubleValue());
        rowsMalloc.push_back (row[6].getDoubleValue());
    };

    // Runs the message loop for `seconds`, writing rows and watching for a condition.
    auto runFor = [&] (double seconds, const std::function<void()>& every = {})
    {
        const auto until = elapsedSeconds() + seconds;

        while (elapsedSeconds() < until)
        {
            pump (100.0);

            if (every)
                every();

            if (elapsedSeconds() >= nextRowAt)
            {
                writeRow();
                nextRowAt += 10.0;
            }
        }
    };

    auto watchStates = [&]
    {
        const auto s = active().getBrain().getSnapshot();
        sawOff = sawOff || (device.mode.load() == Mode::bypass && s.hostOff);
        sawStall = sawStall || (device.mode.load() == Mode::stall && s.audioStalled);
    };

    // Engine footprint: one instance, prepared and running.
    applyConfig (configs[0]);
    configName = configs[0].describe();
    phase = "warm-up";
    runFor (5.0);
    engineMB = footprintMB() - baselineMB;

    // The editor alone: opened, painted in every view and closed 100 times.
    // Graphics caches fill up during the first openings; a leak would keep
    // growing with every one.
    std::vector<double> editorMalloc;
    phase = "editor";

    for (int i = 0; i < 100; ++i)
    {
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (active().createEditor());

            for (const auto* button : { "POSTER", "HISTORY", "COMPACT", "DATA", "BACK" })
            {
                editor->createComponentSnapshot (editor->getLocalBounds());
                clickButton (*editor, button);
            }

            editor->createComponentSnapshot (editor->getLocalBounds());
        }

        pump (20.0);
        editorMalloc.push_back (mallocInUseMB());
    }

    const auto editorGrowth = editorMalloc[99] - editorMalloc[19];
    std::cout << "  editor x100:       allocated " << juce::String (editorMalloc[0], 2) << " MB after 1, "
              << juce::String (editorMalloc[19], 2) << " after 20, " << juce::String (editorMalloc[99], 2) << " after 100 openings\n";

    if (editorGrowth > 0.5)
        failures.add ("editor memory grows with every opening (" + juce::String (editorGrowth, 2) + " MB over 80)");

    while (elapsedSeconds() < minutes * 60.0)
    {
        const auto& config = configs[(size_t) cycle % configs.size()];
        configName = config.describe();
        std::cout << "  cycle " << cycle + 1 << " at " << juce::roundToInt (elapsedSeconds() / 60.0) << " min: " << configName << "\n";
        applyConfig (config);

        // Play, with the editor open for a while (both sizes, HISTORY, DATA).
        phase = "play";
        device.mode = Mode::play;
        runFor (30.0 * scale);
        {
            const auto withoutEditor = footprintMB();
            std::unique_ptr<juce::AudioProcessorEditor> editor (active().createEditor());
            runFor (2.0, watchStates);
            editorMB = std::max (editorMB, footprintMB() - withoutEditor); // the editor and its fonts (no window)

            for (const auto* button : { "POSTER", "HISTORY", "COMPACT", "DATA", "BACK" })
            {
                runFor (6.0 * scale, watchStates);
                editor->createComponentSnapshot (editor->getLocalBounds()); // paints, like a visible window

                if (! clickButton (*editor, button))
                    failures.addIfNotAlreadyThere ("editor button not found: " + juce::String (button));
            }

            runFor (5.0 * scale);
            editor->createComponentSnapshot (editor->getLocalBounds());
        }
        runFor (170.0 * scale, watchStates);

        phase = "silence";
        device.mode = Mode::silence;
        runFor (60.0 * scale, watchStates);

        phase = "off";
        device.mode = Mode::bypass;
        runFor (juce::jmax (3.0, 30.0 * scale), watchStates);

        phase = "stall";
        device.mode = Mode::stall;
        runFor (juce::jmax (3.0, 30.0 * scale), watchStates);

        // A second instance joins (inactive), then the first is removed: the second takes over.
        phase = "takeover";
        device.mode = Mode::play;
        device.stop();
        processors.push_back (std::make_unique<BrearkProcessor>());
        applyConfig (config);
        runFor (juce::jmax (3.0, 20.0 * scale));

        if (processors.back()->isActiveInstance())
            sawTakeover = false; // the newcomer must not listen while the first is there

        device.stop();
        processors.erase (processors.begin());
        applyConfig (config);
        runFor (juce::jmax (3.0, 40.0 * scale));
        sawTakeover = sawTakeover && active().isActiveInstance();

        // HARD, time-warped: drain, unplug (vinyl spin-down), minimum break, plug back in.
        if (cycle % 8 == 0)
        {
            phase = "hard";
            auto& brain = active().getBrain();
            const auto settings = brain.getSettings();
            brain.setSettings ({ Setup::headphones, LevelPreset::loud, Strictness::hard });
            brain.setTimeWarp (60);
            device.hardPhase = true;

            auto sawDownThisTime = false;
            const auto hardStart = elapsedSeconds();

            while (elapsedSeconds() - hardStart < 240.0 && ! (sawDownThisTime && active().getPowerStage() == PowerDown::Stage::run))
            {
                runFor (0.2, watchStates);
                const auto stage = active().getPowerStage();
                sawDownThisTime = sawDownThisTime || stage == PowerDown::Stage::stopped;
                sawPowerDown = sawPowerDown || stage == PowerDown::Stage::stopped;
                sawPowerBack = sawPowerBack || (sawDownThisTime && stage == PowerDown::Stage::spinningUp);
            }

            brain.setTimeWarp (1);
            brain.setSettings (settings);
            runFor (5.0);
            device.hardPhase = false;
        }

        // The median of the cycle's rows: single readings swing with short-lived allocations.
        auto median = [] (std::vector<double>& values)
        {
            std::sort (values.begin(), values.end());
            return values.empty() ? 0.0 : values[values.size() / 2];
        };

        cycleFootprint.push_back (median (rowsFootprint));
        cycleMalloc.push_back (median (rowsMalloc));
        rowsFootprint.clear();
        rowsMalloc.clear();
        ++cycle;
    }

    device.stop();
    writeRow();

    //==============================================================================
    const auto violations = rtcheck.loaded() ? rtcheck.violations() : -1;
    const auto droppedPercent = 100.0 * (double) maxDropped / (double) juce::jmax ((juce::int64) 1, device.samples.load());

    // Growth per hour: the mean of one round of all 8 devices (~1 h), so
    // different buffer sizes and caches filling and emptying (the editor's
    // graphics caches swing ~1 MB) average out. The first hour warms up.
    const auto period = configs.size();

    auto hourlyMeans = [period] (const std::vector<double>& series)
    {
        std::vector<double> means;

        for (size_t start = 0; start + period <= series.size(); start += period)
            means.push_back (std::accumulate (series.begin() + (long) start, series.begin() + (long) (start + period), 0.0) / (double) period);

        return means;
    };

    const auto mallocHours = hourlyMeans (cycleMalloc);
    const auto footprintHours = hourlyMeans (cycleFootprint);
    const auto comparable = mallocHours.size() >= 3;

    auto growth = [] (const std::vector<double>& hours)
    {
        auto worst = 0.0;

        for (size_t h = 2; h < hours.size(); ++h) // hour 1 is the warm-up
            worst = std::max (worst, hours[h] - hours[h - 1]);

        return worst;
    };

    const auto mallocGrowth = growth (mallocHours);
    const auto footprintGrowth = growth (footprintHours);

    juce::StringArray hourList;

    for (const auto mean : mallocHours)
        hourList.add (juce::String (mean, 2));

    std::cout << "\n"
              << "  cycles             " << cycle << " (" << juce::roundToInt (elapsedSeconds() / 60.0) << " min, " << device.blocks.load() << " blocks)\n"
              << "  realtime calls     " << violations << " (allocation, lock, wait, I/O inside brEARk on the audio thread)\n"
              << "  output changed     " << device.mismatchesOutsideHard.load() << " blocks outside HARD mode ("
              << device.mismatches.load() << " incl. power-downs)\n"
              << "  dropped samples    " << maxDropped << " (" << juce::String (droppedPercent, 3)
              << " % of the audio; by design only when the worker is starved)\n"
              << "  memory             engine " << juce::String (engineMB, 2) << " MB, editor +" << juce::String (editorMB, 2)
              << " MB (host baseline " << juce::String (baselineMB, 1) << " MB, malloc " << juce::String (baselineMallocMB, 1) << " MB)\n"
              << "  memory growth      " << (comparable ? "malloc " + juce::String (mallocGrowth, 2) + " MB/h, footprint "
                                                              + juce::String (footprintGrowth, 2) + " MB/h (worst hour after the warm-up hour; "
                                                              + "hourly malloc means " + hourList.joinIntoString (", ") + " MB)"
                                                              + (scale < 1.0 ? " - quick: 7-min \"hours\", not judged" : "")
                                                        : juce::String ("n/a (needs 3 full hours of cycles)")) << "\n"
              << "  CPU                audio thread max " << juce::String (maxAudioMicros, 1) << " us/block, worker max "
              << juce::String (maxWorkerPercent, 3) << " % of one core\n"
              << "  states seen        off " << (sawOff ? "yes" : "NO") << ", stall " << (sawStall ? "yes" : "NO") << ", takeover "
              << (sawTakeover ? "yes" : "NO") << ", power-down " << (sawPowerDown ? "yes" : "NO") << ", back on "
              << (sawPowerBack ? "yes" : "NO") << "\n";

    if (violations != 0)
    {
        failures.add ("realtime violations: " + juce::String (violations));

        if (rtcheck.loaded())
            rtcheck.print();
    }

    if (device.mismatchesOutsideHard.load() != 0) failures.add ("output changed outside HARD mode");
    if (droppedPercent > 1.0)                     failures.add ("more than 1 % of the audio dropped");
    if (engineMB > 6.0)                           failures.add ("engine memory above 6 MB");
    if (comparable && scale == 1.0 && (footprintGrowth > 1.0 || mallocGrowth > 1.0))
        failures.add ("memory grows more than 1 MB per hour");
    if (! sawOff)                                 failures.add ("switched off never seen");
    if (! sawStall)                               failures.add ("stalled audio never seen");
    if (! sawTakeover)                            failures.add ("instance takeover failed");
    if (! sawPowerDown || ! sawPowerBack)         failures.add ("HARD power-down/up not completed");

    processors.clear();

    std::cout << "\n" << (failures.isEmpty() ? "PASS" : "FAIL: " + failures.joinIntoString ("; ")) << "\n";
    return failures.isEmpty() ? 0 : 1;
}
