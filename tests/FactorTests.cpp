#include "engine/FatigueFactors.h"
#include "engine/LevelCreepTracker.h"
#include "engine/PlayheadTracker.h"
#include "engine/RepetitionDetector.h"
#include "engine/SpectrumAnalyser.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace breark
{
namespace
{
constexpr double rate = 48000.0;

struct MusicOptions
{
    double toneGain = 0.3; // the melodic layer (an "EQ tweak" changes only this)
    double jitterDb = 0.0; // humanisation: random level per hit, ± this many dB
};

// A crude "track": 140 BPM kick, 16th hats, random 8th-note tones.
// `pattern` decides the notes and hat noise (same seed = same material),
// `jitter` the per-hit humanisation.
std::vector<float> makeMusic (double seconds, juce::Random& pattern, juce::Random& jitter, MusicOptions options = {})
{
    std::vector<float> out ((size_t) (seconds * rate), 0.0f);
    const auto beat = 60.0 / 140.0;

    for (double t = 0.0; t < seconds; t += beat / 2.0)
    {
        const auto start = (size_t) (t * rate);
        const auto frequency = 200.0 + pattern.nextDouble() * 1800.0;
        const auto isBeat = std::fmod (t, beat) < 1.0e-6;
        const auto hitGain = std::pow (10.0, (jitter.nextDouble() * 2.0 - 1.0) * options.jitterDb / 20.0);

        for (size_t i = 0; i < (size_t) (0.2 * rate) && start + i < out.size(); ++i)
        {
            const auto s = (double) i / rate;
            auto sample = options.toneGain * std::sin (juce::MathConstants<double>::twoPi * frequency * s) * std::exp (-s / 0.08);

            if (isBeat)
                sample += 0.6 * std::sin (juce::MathConstants<double>::twoPi * 55.0 * s) * std::exp (-s / 0.12);

            if (i < (size_t) (0.03 * rate))
                sample += 0.1 * (pattern.nextDouble() * 2.0 - 1.0);

            out[start + i] += (float) (hitGain * sample);
        }
    }

    return out;
}

// A clip of `loopSeconds` playing for `totalSeconds`: every pass has the same
// notes, but its own humanisation. Options switch to `later` halfway through.
std::vector<float> loopClip (double loopSeconds, double totalSeconds, MusicOptions first, MusicOptions later)
{
    std::vector<float> out;
    juce::Random jitter (99);

    while ((double) out.size() < totalSeconds * rate)
    {
        juce::Random pattern (7);
        const auto pass = makeMusic (loopSeconds, pattern, jitter, (double) out.size() < totalSeconds * rate / 2.0 ? first : later);
        out.insert (out.end(), pass.begin(), pass.end());
    }

    out.resize ((size_t) (totalSeconds * rate));
    return out;
}

struct LoopRun
{
    double finalLoopingSeconds = 0.0;
    double maxLoopingSeconds = 0.0;
    double loopLengthSeconds = 0.0;
    float  bestSimilarity = 0.0f; // highest seen during the run
};

// Feeds audio through the spectrum analyser into the detector, one update per second.
LoopRun runDetector (const std::vector<float>& audio)
{
    SpectrumAnalyser spectrum;
    spectrum.prepare (rate);
    RepetitionDetector detector;
    detector.prepare (spectrum.getAnalysedFrameSeconds());

    LoopRun run;
    const auto second = (size_t) rate;

    for (size_t pos = 0; pos + second <= audio.size(); pos += second)
    {
        spectrum.process (audio.data() + pos, audio.data() + pos, (int) second);
        spectrum.drainBandFrames ([&] (const BandFrame& frame) { detector.addFrame (frame); });
        detector.update (1.0, false);

        run.maxLoopingSeconds = std::max (run.maxLoopingSeconds, detector.getLoopingSeconds());
        run.loopLengthSeconds = std::max (run.loopLengthSeconds, detector.getLoopLengthSeconds());
        run.bestSimilarity = std::max (run.bestSimilarity, detector.getBestSimilarity());
    }

    run.finalLoopingSeconds = detector.getLoopingSeconds();
    return run;
}

void report (const char* name, const LoopRun& run)
{
    std::cout << "    " << name << ": looping " << run.finalLoopingSeconds << " s (max " << run.maxLoopingSeconds
              << "), best similarity " << run.bestSimilarity << ", loop length " << run.loopLengthSeconds << " s\n";
}

} // namespace

//==============================================================================
class FactorTests final : public juce::UnitTest
{
public:
    FactorTests() : juce::UnitTest ("Fatigue factors", "brEARk") {}

    void runTest() override
    {
        beginTest ("Factor mapping: neutral inputs change nothing, extremes hit the maxima");
        {
            FactorInputs neutral;
            neutral.presenceToBodyDb = -20.0;
            neutral.highsToBodyDb = -30.0;
            neutral.psrDb = 16.0;
            expectWithinAbsoluteError (computeFatigueFactors (neutral).product(), 1.0, 1.0e-9);

            FactorInputs extreme;
            extreme.presenceToBodyDb = -3.0;
            extreme.psrDb = 5.0;
            extreme.barPlays = 100.0;
            extreme.localHour = 2.0;
            const auto f = computeFatigueFactors (extreme);
            expectWithinAbsoluteError (f.harshness, 1.3, 1.0e-9);
            expectWithinAbsoluteError (f.dynamics, 1.2, 1.0e-9);
            expectWithinAbsoluteError (f.repetition, 1.5, 1.0e-9);
            expectWithinAbsoluteError (f.night, 1.25, 1.0e-9);
        }

        beginTest ("Factor mapping: PSR 8.4 dB (a limited master) → x1.16");
        {
            FactorInputs in;
            in.psrDb = 8.4;
            expectWithinAbsoluteError (computeFatigueFactors (in).dynamics, 1.16, 1.0e-9);
        }

        beginTest ("Night ramps from 22:00, is full 01:00–05:00, off by day");
        {
            auto nightAt = [] (double hour) { FactorInputs in; in.localHour = hour; return computeFatigueFactors (in).night; };
            expectWithinAbsoluteError (nightAt (21.5), 1.0, 1.0e-9);
            expectWithinAbsoluteError (nightAt (23.5), 1.125, 1.0e-9);
            expectWithinAbsoluteError (nightAt (3.0), 1.25, 1.0e-9);
            expectWithinAbsoluteError (nightAt (5.5), 1.0, 1.0e-9);
            expectWithinAbsoluteError (nightAt (14.0), 1.0, 1.0e-9);
        }

        beginTest ("Playhead: an 8-bar loop brace for 10 min counts ~40 plays per bar");
        {
            PlayheadTracker tracker;
            const auto quartersPerSecond = 140.0 / 60.0;
            double ppq = 128.0; // bar 32

            for (double t = 0.0; t < 600.0; t += 0.2)
            {
                tracker.update ({ true, ppq, 4.0 }, t);
                ppq += 0.2 * quartersPerSecond;

                if (ppq >= 160.0) // end of bar 39 → back to bar 32
                    ppq -= 32.0;
            }

            const auto plays = tracker.getCurrentPlays (600.0);
            expect (plays > 30.0 && plays < 45.0, "plays = " + juce::String (plays));
        }

        beginTest ("Playhead: restarting from the same bar counts each time; a play-through counts once");
        {
            PlayheadTracker tracker;

            for (int restart = 0; restart < 5; ++restart)
            {
                tracker.update ({ true, 40.0, 4.0 }, restart * 10.0);
                tracker.update ({ false, 40.0, 4.0 }, restart * 10.0 + 5.0);
            }

            tracker.update ({ true, 40.0, 4.0 }, 60.0);

            // Six plays, each decayed by e^(−age / 30 min) at t = 60 s.
            double expected = 0.0;

            for (double t : { 0.0, 10.0, 20.0, 30.0, 40.0, 60.0 })
                expected += std::exp (-(60.0 - t) / model::barPlaysDecaySeconds);

            expectWithinAbsoluteError (tracker.getCurrentPlays (60.0), expected, 1.0e-6);

            PlayheadTracker once;

            for (double ppq = 0.0; ppq < 400.0; ppq += 0.5)
                once.update ({ true, ppq, 4.0 }, ppq);

            expectWithinAbsoluteError (once.getCurrentPlays (400.0), 1.0, 0.01);

            once.update ({ false, 0.0, 4.0 }, 401.0);
            expectEquals (once.getCurrentPlays (401.0), 0.0);
        }

        beginTest ("Audio loop: an identical 3.37 s clip looping for 3 min is detected");
        {
            const auto run = runDetector (loopClip (3.37, 180.0, {}, {}));
            report ("identical loop", run);
            expect (run.finalLoopingSeconds > 150.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
            expectWithinAbsoluteError (std::fmod (run.loopLengthSeconds + 0.1, 3.37), 0.1, 0.1);
        }

        beginTest ("Audio loop: a humanised loop (every hit ±3 dB at random) is detected");
        {
            MusicOptions humanised;
            humanised.jitterDb = 3.0;
            const auto run = runDetector (loopClip (3.37, 180.0, humanised, humanised));
            report ("humanised loop", run);
            expect (run.finalLoopingSeconds > 150.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
        }

        beginTest ("Audio loop: new material with the same groove is not a loop");
        {
            juce::Random pattern (11), jitter (12);
            MusicOptions humanised;
            humanised.jitterDb = 3.0;
            const auto run = runDetector (makeMusic (180.0, pattern, jitter, humanised));
            report ("new material", run);
            expect (run.maxLoopingSeconds < 10.0, "looping " + juce::String (run.maxLoopingSeconds) + " s");
        }

        beginTest ("Audio loop: a drone never counts");
        {
            std::vector<float> drone ((size_t) (120.0 * rate));

            for (size_t i = 0; i < drone.size(); ++i)
                drone[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) i / rate);

            expectEquals (runDetector (drone).maxLoopingSeconds, 0.0);
        }

        beginTest ("Audio loop: a +3 dB fader move mid-loop doesn't reset the count");
        {
            auto audio = loopClip (3.37, 180.0, {}, {});

            for (size_t i = audio.size() / 2; i < audio.size(); ++i)
                audio[i] *= 1.4125f; // +3 dB

            const auto run = runDetector (audio);
            report ("fader move", run);
            expect (run.finalLoopingSeconds > 150.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
        }

        beginTest ("Audio loop: an EQ tweak mid-loop (melody +6 dB) doesn't reset the count");
        {
            MusicOptions louderMelody;
            louderMelody.toneGain = 0.6;
            const auto run = runDetector (loopClip (3.37, 180.0, {}, louderMelody));
            report ("EQ tweak", run);
            expect (run.finalLoopingSeconds > 150.0, "looping only " + juce::String (run.finalLoopingSeconds) + " s");
        }

        beginTest ("Level creep: the last 10 min vs. an hour ago");
        {
            LevelCreepTracker creep;
            expect (std::isnan (creep.getCreepDb()));

            for (int s = 0; s < 3600; ++s)
                creep.add (s < 600 ? -14.0f : (s < 3000 ? -12.0f : -10.0f), 1.0, false);

            // The older window spans 9 min at −14 and 1 min at −12 → −13.75 dB.
            expectWithinAbsoluteError (creep.getCreepDb(), 3.75, 0.1);
        }
    }
};

static FactorTests factorTests;

} // namespace breark
