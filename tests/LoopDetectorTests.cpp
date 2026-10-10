#include "RealisticMusic.h"
#include "engine/GridLoopDetector.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace breark
{
namespace
{
struct GridRun
{
    double finalLoopingSeconds = 0.0;
    double maxLoopingSeconds = 0.0;
    int loopBars = 0;
    bool hadGrid = false;
    std::vector<float> rhythms, shifts; // diagnostics of every check after the first 30 s
    std::vector<int> bars;
};

// Plays audio through the detector the way the plugin does: 512-sample
// blocks, each with a stamp; one update per second. ppqAt maps a stream
// position to the playhead, so tests can jump like a loop brace.
GridRun runGrid (const std::vector<float>& audio, const test::HouseOptions& options,
                 const std::function<double (juce::int64)>& ppqAt, bool playing = true)
{
    GridLoopDetector detector;
    detector.prepare (options.sampleRate);

    GridRun run;
    constexpr int block = 512;
    const auto second = (juce::int64) options.sampleRate;
    auto nextUpdate = second;

    for (juce::int64 pos = 0; pos + block <= (juce::int64) audio.size(); pos += block)
    {
        BlockStamp stamp;
        stamp.streamPosition = pos;
        stamp.ppq = ppqAt (pos);
        stamp.bpm = options.bpm;
        stamp.playing = playing;

        detector.addStamp (stamp);
        detector.addAudio (audio.data() + pos, audio.data() + pos, block, pos);

        if (pos + block >= nextUpdate)
        {
            detector.update (1.0, false);
            nextUpdate += second;

            run.maxLoopingSeconds = std::max (run.maxLoopingSeconds, detector.getLoopingSeconds());

            if (pos > 30 * second)
            {
                run.rhythms.push_back (detector.getBestRhythm());
                run.shifts.push_back (detector.getBestShiftDb());
                run.bars.push_back (detector.getBestBars());
            }
            run.hadGrid = run.hadGrid || detector.hasGrid();
        }
    }

    run.finalLoopingSeconds = detector.getLoopingSeconds();
    run.loopBars = detector.getLoopBars();
    return run;
}

std::function<double (juce::int64)> continuousPlayhead (const test::HouseOptions& options)
{
    const auto samplesPerQuarter = 60.0 * options.sampleRate / options.bpm;
    return [samplesPerQuarter] (juce::int64 pos) { return (double) pos / samplesPerQuarter; };
}

int secondsToBars (double seconds, double bpm) { return (int) std::ceil (seconds / (4.0 * 60.0 / bpm)); }

void report (const char* name, const GridRun& run)
{
    auto spread = [] (std::vector<float> v)
    {
        std::sort (v.begin(), v.end());
        auto at = [&] (double q) { return v.empty() ? 0.0f : v[(size_t) (q * (double) (v.size() - 1))]; };
        return juce::String (at (0.0), 2) + " / " + juce::String (at (0.5), 2) + " / " + juce::String (at (1.0), 2);
    };

    std::cout << "    " << name << ": looping " << juce::roundToInt (run.finalLoopingSeconds) << " s (max "
              << juce::roundToInt (run.maxLoopingSeconds) << "), " << run.loopBars << "-bar loop | rhythm min/median/max "
              << spread (run.rhythms) << " | shift dB " << spread (run.shifts) << "\n";
}
} // namespace

//==============================================================================
class LoopDetectorTests final : public juce::UnitTest
{
public:
    LoopDetectorTests() : juce::UnitTest ("Loop detection (tempo grid)", "brEARk") {}

    void runTest() override
    {
        const auto twoBarLoop = [] (int bar) { return bar % 2 == 0 ? 0 : 3; };

        beginTest ("Session View: an identical 2-bar loop is a 2-bar loop");
        {
            test::HouseOptions options;
            juce::Random human (1);
            const auto audio = test::renderHouse (secondsToBars (150.0, options.bpm), twoBarLoop, options, human);
            const auto run = runGrid (audio, options, continuousPlayhead (options));
            report ("identical", run);

            expect (run.finalLoopingSeconds > 130.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
            expectEquals (run.loopBars, 2);
        }

        beginTest ("Session View: a humanised 2-bar loop at 147 BPM (hats ±3 dB, free LFO ±3 dB) is detected");
        {
            test::HouseOptions options;
            options.bpm = 147.0;
            options.hatJitterDb = 3.0;
            options.padLfoDb = 3.0;
            juce::Random human (2);
            const auto audio = test::renderHouse (secondsToBars (150.0, options.bpm), twoBarLoop, options, human);
            const auto run = runGrid (audio, options, continuousPlayhead (options));
            report ("humanised", run);

            expect (run.finalLoopingSeconds > 130.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
            expectEquals (run.loopBars, 2);
        }

        beginTest ("Session View: heavy humanisation (hats ±6 dB, LFO ±4 dB at another rate) is still a loop");
        {
            test::HouseOptions options;
            options.bpm = 132.0;
            options.hatJitterDb = 6.0;
            options.padLfoDb = 4.0;
            options.padLfoHz = 0.37;
            juce::Random human (8);
            const auto audio = test::renderHouse (secondsToBars (150.0, options.bpm), twoBarLoop, options, human);
            const auto run = runGrid (audio, options, continuousPlayhead (options));
            report ("heavy humanised", run);

            expect (run.finalLoopingSeconds > 120.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
        }

        beginTest ("Same drums, new chords every bar: not a loop");
        {
            test::HouseOptions options;
            options.hatJitterDb = 3.0;
            juce::Random human (3), harmony (4);
            std::vector<int> chords;

            for (int bar = 0; bar < 200; ++bar)
                chords.push_back (harmony.nextInt (6));

            const auto audio = test::renderHouse (secondsToBars (150.0, options.bpm),
                                                  [&] (int bar) { return chords[(size_t) bar]; }, options, human);
            const auto run = runGrid (audio, options, continuousPlayhead (options));
            report ("new chords", run);

            expect (run.maxLoopingSeconds < 10.0, "looping " + juce::String (run.maxLoopingSeconds) + " s");
        }

        beginTest ("Arrangement loop brace: an 8-bar passage with the playhead jumping back is an 8-bar loop");
        {
            test::HouseOptions options;
            options.bpm = 125.0; // 8 bars = exactly 1323 blocks: the jump lands on a block boundary, as hosts do it
            juce::Random human (5);
            const int passage[] = { 0, 3, 2, 5, 1, 4, 0, 2 };
            const auto audio = test::renderHouse (secondsToBars (150.0, options.bpm),
                                                  [&] (int bar) { return passage[bar % 8]; }, options, human);

            const auto samplesPerQuarter = 60.0 * options.sampleRate / options.bpm;
            const auto passageSamples = 32.0 * samplesPerQuarter;
            const auto run = runGrid (audio, options, [=] (juce::int64 pos)
            {
                return 64.0 + std::fmod ((double) pos, passageSamples) / samplesPerQuarter; // bars 17–24, looped
            });
            report ("loop brace", run);

            expect (run.finalLoopingSeconds > 100.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
            expectEquals (run.loopBars, 8);
        }

        beginTest ("A +3 dB fader move mid-loop doesn't reset the count");
        {
            test::HouseOptions options;
            juce::Random human (6);
            auto audio = test::renderHouse (secondsToBars (150.0, options.bpm), twoBarLoop, options, human);

            for (size_t i = audio.size() / 2; i < audio.size(); ++i)
                audio[i] *= 1.4125f;

            const auto run = runGrid (audio, options, continuousPlayhead (options));
            report ("fader move", run);
            expect (run.finalLoopingSeconds > 130.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
        }

        beginTest ("An EQ tweak mid-loop (pad +6 dB) pauses, but doesn't reset, the count");
        {
            test::HouseOptions before, after;
            after.padGainDb = 6.0;
            juce::Random human (9);
            const auto bars = secondsToBars (75.0, before.bpm) / 2 * 2; // whole loops in each half
            auto audio = test::renderHouse (bars, twoBarLoop, before, human);
            const auto second = test::renderHouse (bars, twoBarLoop, after, human);
            audio.insert (audio.end(), second.begin(), second.end());

            const auto run = runGrid (audio, before, continuousPlayhead (before));
            report ("EQ tweak", run);
            expect (run.finalLoopingSeconds > 120.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
        }

        beginTest ("Transport stopped: no grid, no loop");
        {
            test::HouseOptions options;
            juce::Random human (7);
            const auto audio = test::renderHouse (secondsToBars (60.0, options.bpm), twoBarLoop, options, human);
            const auto run = runGrid (audio, options, continuousPlayhead (options), false);

            expect (! run.hadGrid);
            expectEquals (run.maxLoopingSeconds, 0.0);
        }

        beginTest ("A drone never counts");
        {
            test::HouseOptions options;
            std::vector<float> drone ((size_t) (90.0 * options.sampleRate));

            for (size_t i = 0; i < drone.size(); ++i)
                drone[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) i / options.sampleRate);

            expectEquals (runGrid (drone, options, continuousPlayhead (options)).maxLoopingSeconds, 0.0);
        }
    }
};

static LoopDetectorTests loopDetectorTests;

} // namespace breark
