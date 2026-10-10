#include "Persistence.h"

namespace breark::persistence
{
namespace
{
const juce::StringArray setupNames { "monitors", "headphones" };
const juce::StringArray presetNames { "quiet", "normal", "loud" };

juce::var toVar (const DayRecord& record)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("day", record.day);
    object->setProperty ("dose", record.dose);
    object->setProperty ("listeningSeconds", record.listeningSeconds);
    object->setProperty ("breaks", record.breaks);
    return object.release();
}

DayRecord dayRecordFromVar (const juce::var& v)
{
    return { (int) v["day"], (double) v["dose"], (double) v["listeningSeconds"], (int) v["breaks"] };
}

template <typename Enum>
Enum enumFromName (const juce::StringArray& names, const juce::var& value, Enum fallback)
{
    const auto index = names.indexOf (value.toString());
    return index >= 0 ? (Enum) index : fallback;
}
} // namespace

juce::File defaultFile()
{
    // Developer tools (null test, pluginval) redirect this, so they never touch
    // the real listening history.
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("BREARK_STATE_FILE", {}); path.isNotEmpty())
        return juce::File (path);

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/brEARk/state.json");
}

bool save (const juce::File& file, const PersistedState& state)
{
    const auto& e = state.exposure;

    auto settings = std::make_unique<juce::DynamicObject>();
    settings->setProperty ("setup", setupNames[(int) state.settings.setup]);
    settings->setProperty ("preset", presetNames[(int) state.settings.preset]);

    juce::Array<juce::var> history;

    for (const auto& record : e.history)
        history.add (toVar (record));

    auto exposure = std::make_unique<juce::DynamicObject>();
    exposure->setProperty ("day", e.day);
    exposure->setProperty ("battery", e.battery);
    exposure->setProperty ("doseToday", e.doseToday);
    exposure->setProperty ("listeningSecondsToday", e.listeningSecondsToday);
    exposure->setProperty ("silenceSeconds", e.silenceSeconds);
    exposure->setProperty ("secondsSinceBreak", e.secondsSinceBreak);
    exposure->setProperty ("breaksToday", e.breaksToday);
    exposure->setProperty ("referenceLufs", e.referenceLufs);
    exposure->setProperty ("history", history);

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("version", formatVersion);
    root->setProperty ("savedAtMs", state.savedAtMs);
    root->setProperty ("settings", settings.release());
    root->setProperty ("exposure", exposure.release());

    if (! file.getParentDirectory().createDirectory())
        return false;

    return file.replaceWithText (juce::JSON::toString (juce::var (root.release())));
}

std::optional<PersistedState> load (const juce::File& file)
{
    if (! file.existsAsFile())
        return std::nullopt;

    const auto root = juce::JSON::parse (file.loadFileAsString());

    if (! root.isObject() || (int) root["version"] != formatVersion)
        return std::nullopt;

    const auto& s = root["settings"];
    const auto& e = root["exposure"];

    if (! s.isObject() || ! e.isObject())
        return std::nullopt;

    PersistedState state;
    state.savedAtMs = (juce::int64) root["savedAtMs"];
    state.settings.setup = enumFromName (setupNames, s["setup"], Setup::monitors);
    state.settings.preset = enumFromName (presetNames, s["preset"], LevelPreset::normal);

    auto& x = state.exposure;
    x.day = (int) e["day"];
    x.battery = juce::jlimit (0.0, 100.0, (double) e["battery"]);
    x.doseToday = juce::jmax (0.0, (double) e["doseToday"]);
    x.listeningSecondsToday = (double) e["listeningSecondsToday"];
    x.silenceSeconds = (double) e["silenceSeconds"];
    x.secondsSinceBreak = (double) e["secondsSinceBreak"];
    x.breaksToday = (int) e["breaksToday"];
    x.referenceLufs = e.hasProperty ("referenceLufs") ? (double) e["referenceLufs"] : model::initialReferenceLufs;

    if (const auto* history = e["history"].getArray())
        for (const auto& record : *history)
            x.history.push_back (dayRecordFromVar (record));

    return state;
}

} // namespace breark::persistence
