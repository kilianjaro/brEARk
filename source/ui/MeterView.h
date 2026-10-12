#pragma once

#include "Look.h"
#include "dsp/PowerDown.h"
#include "engine/Snapshot.h"

namespace breark::ui
{
enum class MeterSize { compact, poster };

juce::Rectangle<int> meterBounds (MeterSize size); // 520 × 320 or 960 × 600

// A value as shown, with its hover explanation (ui/Tooltips.h).
struct Item
{
    juce::String text, tip;
};

struct Stat
{
    juce::String label, value, tip;
};

// Everything the meter view shows, already in words.
struct MeterModel
{
    juce::String word { "FRESH." }; // the poster word
    juce::String status;            // one line above it
    juce::String battery { "100 %" };
    double batteryFraction = 1.0;   // the height of the black mass
    bool alarm = false;             // the whole window turns signal red (STOP., UNPLUGGED.)
    bool massRed = false;           // the mass turns red near empty
    std::vector<std::vector<Item>> compactLines; // two data lines (COMPACT)
    std::vector<Stat> stats;        // six columns (POSTER)
    std::vector<Item> factors;      // why the battery drains (POSTER)
    juce::String coach;             // up to two lines
};

MeterModel makeMeterModel (const Snapshot& snapshot, const juce::String& coachLine,
                           bool activeInstance, PowerDown::Stage powerStage);

// The explanation for what's under the mouse, or an empty string.
juce::String tooltipAt (const MeterModel& model, MeterSize size, juce::Point<float> position, const Fonts& fonts);

//==============================================================================
// Design B: the window is the meter. A mass fills it from the bottom to the
// battery level; everything is drawn twice, and where the mass cuts through
// the word or the numbers they invert.
void paintMeter (juce::Graphics& g, const MeterModel& model, MeterSize size, const Fonts& fonts);

} // namespace breark::ui
