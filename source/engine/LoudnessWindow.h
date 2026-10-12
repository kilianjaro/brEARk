#pragma once

#include <array>
#include <cmath>
#include <limits>

namespace breark
{
//==============================================================================
// The energy mean of the short-term loudness over the last `windowSeconds`,
// time-weighted; silence counts as silence. Answers "has it been this loud for
// the last 2 minutes?" (the LOUD pop-up, M9), not "was there a loud moment".
// Worker thread only, fixed memory.
class LoudnessWindow
{
public:
    explicit LoudnessWindow (double windowSecondsToUse) : windowSeconds (windowSecondsToUse) {}

    // One model tick. lufs = −inf for silence.
    void add (float lufs, double seconds) noexcept
    {
        if (! (seconds > 0.0))
            return;

        if (count == (int) entries.size()) // ticks far shorter than expected: drop the oldest
            dropOldest();

        const auto energy = std::isfinite (lufs) ? std::pow (10.0, lufs / 10.0) : 0.0;
        entries[(size_t) ((first + count) % (int) entries.size())] = { seconds, energy };
        ++count;
        coveredSeconds += seconds;
        energySeconds += energy * seconds;

        while (count > 1 && coveredSeconds - entries[(size_t) first].seconds >= windowSeconds)
            dropOldest();
    }

    void reset() noexcept
    {
        first = count = 0;
        coveredSeconds = energySeconds = 0.0;
    }

    // −inf until the window is (almost) full, or when it was silent throughout.
    float getLufs() const noexcept
    {
        if (coveredSeconds < 0.95 * windowSeconds || ! (energySeconds > 0.0))
            return -std::numeric_limits<float>::infinity();

        return (float) (10.0 * std::log10 (energySeconds / coveredSeconds));
    }

private:
    void dropOldest() noexcept
    {
        const auto& oldest = entries[(size_t) first];
        coveredSeconds -= oldest.seconds;
        energySeconds -= oldest.energy * oldest.seconds;
        first = (first + 1) % (int) entries.size();
        --count;

        if (count == 0) // no drift from repeated subtraction
            coveredSeconds = energySeconds = 0.0;
    }

    struct Entry
    {
        double seconds = 0.0, energy = 0.0;
    };

    const double windowSeconds;
    std::array<Entry, 512> entries {}; // ~1 s model ticks: room for 8 min
    int first = 0, count = 0;
    double coveredSeconds = 0.0, energySeconds = 0.0;
};

} // namespace breark
