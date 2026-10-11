#pragma once

#include "Look.h"
#include "dsp/PowerDown.h"
#include "engine/Snapshot.h"

namespace breark::ui
{
enum class MeterSize { compact, poster };

juce::Rectangle<int> meterBounds (MeterSize size); // 520 × 320 or 960 × 600

// Everything the meter view shows, already in words.
struct MeterModel
{
    juce::String word { "FRESH." }; // the poster word
    juce::String status;            // one line above it
    juce::String battery { "100 %" };
    double batteryFraction = 1.0;   // the height of the black mass
    bool alarm = false;             // the whole window turns signal red (STOP., UNPLUGGED.)
    bool massRed = false;           // the mass turns red near empty
    juce::StringArray compactLines; // two data lines (COMPACT)
    juce::StringArray statLabels, statValues; // six columns (POSTER)
    juce::String factors;           // the drain factors (POSTER)
    juce::String coach;
};

MeterModel makeMeterModel (const Snapshot& snapshot, const juce::String& coachLine,
                           bool activeInstance, PowerDown::Stage powerStage);

//==============================================================================
// Design B: the window is the meter. A mass fills it from the bottom to the
// battery level; everything is drawn twice, and where the mass cuts through
// the word or the numbers they invert.
void paintMeter (juce::Graphics& g, const MeterModel& model, MeterSize size, const Fonts& fonts);

} // namespace breark::ui
