#include "PowerDown.h"
#include "engine/ModelConstants.h"

namespace breark
{
namespace
{
// Longest lag behind the live signal: half the spin-down (the read head
// averages half speed), plus room for a block.
constexpr double ringSeconds = 0.5 * model::spinDownSeconds + 0.25;
constexpr double spinDownCrossfadeSeconds = 0.005;
constexpr double spinUpCrossfadeSeconds = 0.010;
} // namespace

PowerDown::Ring::Ring (double ringSampleRate, double seconds)
    : sampleRate (ringSampleRate)
{
    const auto size = juce::nextPowerOfTwo ((int) std::ceil (ringSampleRate * seconds));
    mask = size - 1;

    for (auto& channel : channels)
        channel.assign ((size_t) size, 0.0f);
}

PowerDown::~PowerDown()
{
    // Audio has stopped by now.
    delete active;
    delete incoming.exchange (nullptr);
    delete retired.exchange (nullptr);
}

//==============================================================================
void PowerDown::manageBuffer (bool hardMode, double sampleRate)
{
    delete retired.exchange (nullptr); // whatever the audio thread let go of

    if (! hardMode)
    {
        delete incoming.exchange (nullptr);

        if (activeRate.load() > 0.0)
            releaseRequested.store (true);

        return;
    }

    releaseRequested.store (false);

    if (sampleRate <= 0.0 || juce::exactlyEqual (activeRate.load(), sampleRate))
        return;

    // Only the message thread deletes rings, so reading a pending one is safe.
    if (const auto* pending = incoming.load(); pending != nullptr && juce::exactlyEqual (pending->sampleRate, sampleRate))
        return;

    delete incoming.exchange (new Ring (sampleRate, ringSeconds));
}

void PowerDown::adoptBuffers() noexcept
{
    // Never swap mid-ramp, and only hand back one buffer at a time: `retired`
    // can only be emptied by the other thread, so check-then-act is safe here.
    if (stage != Stage::run || retired.load() != nullptr)
        return;

    if (auto* next = incoming.exchange (nullptr))
    {
        retired.store (active);
        active = next;
        activeRate.store (next->sampleRate);
        return;
    }

    if (releaseRequested.load() && active != nullptr)
    {
        retired.store (active);
        active = nullptr;
        activeRate.store (0.0);
        releaseRequested.store (false);
    }
}

void PowerDown::begin (Stage newStage) noexcept
{
    const auto down = newStage == Stage::spinningDown;

    written = 0;
    readPosition = -3.0; // three samples behind the live signal: room for interpolation
    elapsed = 0;
    rampLength = std::max<juce::int64> (1, (juce::int64) ((down ? model::spinDownSeconds : model::spinUpSeconds) * rate));
    crossfadeLength = std::max<juce::int64> (1, (juce::int64) ((down ? spinDownCrossfadeSeconds : spinUpCrossfadeSeconds) * rate));
    spinUpArmed = false;
    setStage (newStage);
}

void PowerDown::setStage (Stage newStage) noexcept
{
    stage = newStage;
    publishedStage.store (newStage, std::memory_order_relaxed);
}

float PowerDown::hermite (const std::vector<float>& ring, int mask, double position) noexcept
{
    // 4-point, 3rd-order Hermite (Catmull-Rom).
    const auto i = (juce::int64) std::floor (position);
    const auto t = (float) (position - (double) i);

    const auto xm1 = ring[(size_t) ((i - 1) & mask)];
    const auto x0  = ring[(size_t) (i & mask)];
    const auto x1  = ring[(size_t) ((i + 1) & mask)];
    const auto x2  = ring[(size_t) ((i + 2) & mask)];

    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

    return ((c3 * t + c2) * t + c1) * t + x0;
}

} // namespace breark
