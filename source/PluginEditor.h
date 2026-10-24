#pragma once

#include "DebugView.h"
#include "PluginProcessor.h"
#include "ui/HistoryView.h"
#include "ui/Look.h"
#include "ui/MeterView.h"

//==============================================================================
// The brEARk window: design B, the window is the meter (ui/MeterView).
// COMPACT 520 × 320 or POSTER 960 × 600; POSTER adds the stats and HISTORY.
// DATA opens the diagnostics. Refreshed at a calm 4 Hz.
class BrearkEditor final : public juce::AudioProcessorEditor,
                           public juce::TooltipClient,
                           private juce::Timer
{
public:
    explicit BrearkEditor (BrearkProcessor&);
    ~BrearkEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Hover explanations for the painted values (buttons have their own).
    juce::String getTooltip() override;

private:
    void timerCallback() override;
    void applySize();
    void showData (bool show);
    breark::ui::MeterSize meterSize() const noexcept;

    BrearkProcessor& brearkProcessor;
    breark::ui::Look look;
    juce::SharedResourcePointer<breark::ui::Fonts> fonts;
    breark::Snapshot snapshot;
    breark::ui::MeterModel model;

    juce::TextButton setupButton, modeButton, soundButton, sizeButton, dataButton { "DATA" };
    juce::TextButton historyButton { "HISTORY" }, breakButton { "BREAK" };
    SegmentedControl levelSwitch { { "QUIET", "NORMAL", "LOUD" } }; // how loud on your ears (M11)
    breark::ui::HistoryOverlay history;
    DebugView data;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrearkEditor)
};
