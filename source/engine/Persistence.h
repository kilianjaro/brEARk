#pragma once

#include "ExposureModel.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace breark
{
struct PersistedState
{
    Settings settings;
    ExposureState exposure;
    juce::int64 savedAtMs = 0; // wall clock, to credit the time Live was closed as recovery
};

// state.json in ~/Library/Application Support/brEARk. Written atomically
// (temp file + rename), so a crash mid-write never corrupts it. Worker thread only.
namespace persistence
{
inline constexpr int formatVersion = 1;

juce::File defaultFile();
bool save (const juce::File& file, const PersistedState& state);
std::optional<PersistedState> load (const juce::File& file); // nullopt: missing, corrupt or other version
} // namespace persistence

} // namespace breark
