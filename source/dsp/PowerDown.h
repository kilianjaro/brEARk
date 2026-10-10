#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace breark
{
//==============================================================================
// Hard mode's "pulling the plug": the only code allowed to change the audio.
//
//   run           untouched: the buffer is never written (bit-exact)
//   spinningDown  a turntable losing power: the audio is read from a ring
//                 buffer at a rate falling 1 → 0 (raised cosine, ~3.5 s);
//                 the level follows the rate (a cartridge's output is
//                 proportional to stylus velocity), ending in digital silence
//   stopped       exact zeros
//   spinningUp    rate 0 → ~1.35 → 1 (~0.8 s), shaped so the total distance
//                 travelled equals the elapsed time: the read head lands back
//                 on the live signal, then a 10 ms crossfade hands over to the
//                 untouched input
//
// The ring buffer only exists while Hard mode is on. The message thread
// creates and destroys it; the audio thread adopts and retires it through
// atomics, so it never allocates, locks or waits. Without a buffer the same
// curves are applied as a plain fade. Exports always pass untouched.
class PowerDown
{
public:
    enum class Stage { run, spinningDown, stopped, spinningUp };

    struct Ring
    {
        Ring (double sampleRate, double seconds);

        double sampleRate;
        int mask;
        std::array<std::vector<float>, 2> channels;
    };

    PowerDown() = default;
    ~PowerDown();

    //==============================================================================
    // Message thread (or while audio is stopped).
    void prepare (double sampleRate) noexcept { rate = sampleRate; }

    // Hard mode on: makes sure a ring buffer for this sample rate is on its way
    // to the audio thread. Hard mode off: asks the audio thread to let go of it.
    void manageBuffer (bool hardMode, double sampleRate);

    Stage getStage() const noexcept { return publishedStage.load (std::memory_order_relaxed); }

    //==============================================================================
    // Audio thread.
    template <typename SampleType>
    void process (juce::AudioBuffer<SampleType>& buffer, bool unplug, bool exporting) noexcept;

private:
    void adoptBuffers() noexcept;
    void begin (Stage newStage) noexcept;
    void setStage (Stage newStage) noexcept;

    template <typename SampleType>
    static bool isAudible (const juce::AudioBuffer<SampleType>& buffer) noexcept;

    static float hermite (const std::vector<float>& ring, int mask, double position) noexcept;

    double rate = 44100.0;
    Stage stage = Stage::run;
    std::atomic<Stage> publishedStage { Stage::run };
    bool spinUpArmed = false;

    // Progress of the current ramp, in samples.
    juce::int64 written = 0;   // samples written into the ring since the ramp began
    double readPosition = 0.0; // fractional, in the same coordinates
    juce::int64 elapsed = 0;
    juce::int64 rampLength = 0;
    juce::int64 crossfadeLength = 0;

    // Ring buffer handover: message thread → audio thread → message thread.
    Ring* active = nullptr;                    // audio thread only
    std::atomic<Ring*> incoming { nullptr };   // new buffer, waiting to be adopted
    std::atomic<Ring*> retired { nullptr };    // old buffer, waiting to be deleted
    std::atomic<bool> releaseRequested { false };
    std::atomic<double> activeRate { 0.0 };    // the adopted buffer's rate, 0 = none
};

//==============================================================================
template <typename SampleType>
bool PowerDown::isAudible (const juce::AudioBuffer<SampleType>& buffer) noexcept
{
    constexpr auto threshold = (SampleType) 0.001; // −60 dBFS

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        if (buffer.getMagnitude (ch, 0, buffer.getNumSamples()) > threshold)
            return true;

    return false;
}

template <typename SampleType>
void PowerDown::process (juce::AudioBuffer<SampleType>& buffer, bool unplug, bool exporting) noexcept
{
    // An export never reaches the ears: always untouched. A ramp in progress
    // jumps to its end state, so it never resumes on stale buffer content.
    if (exporting)
    {
        if (stage == Stage::spinningDown) setStage (Stage::stopped);
        if (stage == Stage::spinningUp)   setStage (Stage::run);
        return;
    }

    adoptBuffers();

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = std::min (2, buffer.getNumChannels());

    switch (stage)
    {
        case Stage::run:
            if (unplug)
                begin (Stage::spinningDown);
            else if (spinUpArmed && isAudible (buffer))
                begin (Stage::spinningUp);
            else
                return; // untouched
            break;

        case Stage::stopped:
            if (unplug)
            {
                buffer.clear();
                return;
            }

            spinUpArmed = true; // the break is over: spin up when there's sound
            setStage (Stage::run);

            if (! isAudible (buffer))
                return; // untouched; the spin-up waits for the next sound

            begin (Stage::spinningUp);
            break;

        case Stage::spinningDown:
        case Stage::spinningUp:
            break;
    }

    constexpr auto pi = juce::MathConstants<double>::pi;
    const auto ring = active;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto progress = std::min (1.0, (double) elapsed / (double) rampLength);
        const auto spinningDown = stage == Stage::spinningDown;

        // Turntable speed; the cartridge level follows it (never above 1).
        const auto speed = spinningDown ? 0.5 * (1.0 + std::cos (pi * progress))
                                        : (progress < 1.0 ? progress + 0.25 * pi * std::sin (pi * progress) : 1.0);
        const auto level = (float) std::min (1.0, speed);

        // Spin-down starts by crossfading in from the dry signal; spin-up ends
        // by crossfading out to it.
        const auto towardsDry = spinningDown ? 1.0 - std::min (1.0, (double) elapsed / (double) crossfadeLength)
                                             : std::max (0.0, (double) (elapsed - rampLength) / (double) crossfadeLength);

        const auto readable = ring != nullptr && readPosition >= 1.0 && readPosition + 2.0 < (double) written + 1.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto dry = (float) data[i];

            if (ring != nullptr)
                ring->channels[(size_t) ch][(size_t) (written & ring->mask)] = dry;

            // Without a ring buffer: the same curve as a plain fade. Until the
            // read head has history: dry when spinning down, silence when up.
            const auto played = ring == nullptr ? dry
                              : (readable ? hermite (ring->channels[(size_t) ch], ring->mask, readPosition)
                                          : (spinningDown ? dry : 0.0f));

            const auto wet = level * played;
            data[i] = (SampleType) ((1.0 - towardsDry) * wet + towardsDry * dry);
        }

        ++written;
        readPosition += speed;
        ++elapsed;

        if (spinningDown && elapsed >= rampLength)
        {
            setStage (Stage::stopped);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.clear (ch, i + 1, numSamples - i - 1);

            break;
        }

        if (! spinningDown && elapsed >= rampLength + crossfadeLength)
        {
            setStage (Stage::run); // the rest of the block stays untouched
            break;
        }
    }

    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        if (stage != Stage::run)
            buffer.clear (ch, 0, numSamples);
}

} // namespace breark
