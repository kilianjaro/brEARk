#include "PluginEditor.h"

namespace
{
const auto paper = juce::Colour (0xffededed);
const auto ink   = juce::Colour (0xff0a0a0a);

constexpr int rowHeight = 19;

// Values below −100 dB are numerical decay, not signal.
juce::String db (float value, int decimals = 1)
{
    return (std::isfinite (value) && value > -100.0f) ? juce::String (value, decimals) : juce::String ("-inf");
}

juce::String yesNo (bool value)
{
    return value ? "YES" : "NO";
}

juce::String clock (double seconds)
{
    const auto minutes = juce::roundToInt (seconds / 60.0);
    return juce::String (minutes / 60).paddedLeft ('0', 2) + ":" + juce::String (minutes % 60).paddedLeft ('0', 2);
}

void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, ink);
    button.setColour (juce::TextButton::textColourOffId, paper);
    button.setWantsKeyboardFocus (false);
}
} // namespace

//==============================================================================
BrearkEditor::BrearkEditor (BrearkProcessor& p)
    : AudioProcessorEditor (p),
      brearkProcessor (p)
{
    for (auto* button : { &setupButton, &presetButton, &warpButton })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }

    auto& brain = brearkProcessor.getBrain();

    setupButton.onClick = [&brain]
    {
        auto settings = brain.getSettings();
        settings.setup = settings.setup == breark::Setup::monitors ? breark::Setup::headphones : breark::Setup::monitors;
        brain.setSettings (settings);
    };

    presetButton.onClick = [&brain]
    {
        auto settings = brain.getSettings();
        settings.preset = (breark::LevelPreset) (((int) settings.preset + 1) % 3);
        brain.setSettings (settings);
    };

    warpButton.onClick = [this, &brain] { brain.setTimeWarp (snapshot.timeWarp == 1 ? 60 : 1); };

    setSize (560, 640);
    timerCallback();
    startTimerHz (4);
}

BrearkEditor::~BrearkEditor()
{
    stopTimer();
}

void BrearkEditor::timerCallback()
{
    snapshot = brearkProcessor.getBrain().getSnapshot();
    updateButtons();
    repaint();
}

void BrearkEditor::updateButtons()
{
    // Show the requested settings immediately; the model follows within a second.
    const auto settings = brearkProcessor.getBrain().getSettings();
    const char* presetNames[] = { "QUIET (~70 dB)", "NORMAL (~80 dB)", "LOUD (~90 dB)" };

    setupButton.setButtonText (settings.setup == breark::Setup::monitors ? "MONITORS" : "HEADPHONES");
    presetButton.setButtonText (presetNames[(int) settings.preset]);
    warpButton.setButtonText (snapshot.timeWarp == 1 ? "TIME x1" : "TIME x" + juce::String (snapshot.timeWarp) + " (NOT SAVED)");
}

void BrearkEditor::resized()
{
    auto row = getLocalBounds().reduced (24).removeFromBottom (rowHeight + 34).removeFromTop (28);

    setupButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    presetButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (8);
    warpButton.setBounds (row.removeFromLeft (190));
}

void BrearkEditor::paint (juce::Graphics& g)
{
    g.fillAll (snapshot.timeWarp > 1 ? paper.darker (0.08f) : paper);
    g.setColour (ink);

    auto area = getLocalBounds().reduced (24);

    g.setFont (juce::Font (juce::FontOptions (88.0f, juce::Font::bold)));
    g.drawText ("brEARk.", area.removeFromTop (96), juce::Justification::topLeft, false);

    g.fillRect (area.removeFromTop (4));
    area.removeFromTop (14);

    const auto& s = snapshot;
    const auto micro = juce::String::fromUTF8 ("\xc2\xb5s");
    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));

    const auto status = ! brearkProcessor.isActiveInstance() ? juce::String ("INACTIVE - ANOTHER brEARk IS LISTENING")
                      : s.rendering                         ? juce::String ("EXPORT DETECTED - NOT COUNTING")
                      : s.recovering                        ? juce::String ("RECOVERING")
                      : s.silent                            ? juce::String ("SILENCE")
                                                            : juce::String ("LISTENING");

    const juce::StringArray rows {
        "STATUS        " + status,
        "LOUDNESS      M " + db (s.lufsMomentary) + "   S " + db (s.lufsShortTerm) + " LUFS   REF " + db (s.referenceLufs),
        "LEVEL (A)     " + db (s.levelA) + " dBFS(A)",
        "PEAK / PSR    " + db (s.peakShortTerm) + " dBFS   PSR " + (s.silent ? juce::String ("-") : db (s.peakShortTerm - s.lufsShortTerm)) + " dB",
        "SPECTRUM      PRESENCE " + db (s.presence) + " dB   HIGHS " + db (s.highs) + " dB",
        "",
        "EAR BATTERY   " + juce::String (s.battery, 1) + " %",
        "DOSE          " + juce::String (s.doseToday, 1) + " % TODAY   " + juce::String (s.doseWeek, 1) + " % WEEK  (WHO)",
        "EST. LEVEL    ~" + db (s.perceivedSpl, 0) + " dB PERCEIVED   ~" + db (s.doseSpl, 0) + " dB(A) DOSE",
        "TODAY         " + clock (s.listeningSecondsToday) + " LISTENED   " + clock (s.secondsSinceBreak)
            + " SINCE BREAK   " + juce::String (s.breaksToday) + " BREAKS",
        "NEXT BREAK    " + juce::String (juce::roundToInt (s.suggestedBreakMinutes)) + " MIN SUGGESTED",
        "",
        "ENGINE        " + juce::String (s.sampleRate, 0) + " HZ  " + wrapper.toUpperCase()
            + "  " + (processor.isUsingDoublePrecision() ? "64" : "32") + "-BIT  FFT " + juce::String (s.fftSize),
        "AUDIO THREAD  " + juce::String (s.audioMicrosAverage, 2) + " " + micro + " AVG   "
            + juce::String (s.audioMicrosMax, 2) + " " + micro + " MAX  / BLOCK",
        "WORKER        " + juce::String (s.workerLoadPercent, 3) + " % OF ONE CORE   "
            + juce::String (s.samplesDropped) + " DROPPED   WAKE " + juce::String (s.workerWakeMsAverage, 0) + " MS",
        "EXPORTS       " + juce::String (s.rendersSeen) + " SEEN   HOST FLAG " + yesNo (s.lastRenderHadHostFlag)
            + "   SPEED " + juce::String (s.renderSpeed, 2) + "x",
    };

    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

    for (const auto& row : rows)
        g.drawText (row, area.removeFromTop (rowHeight), juce::Justification::centredLeft, false);

    g.drawText ("> M2. Counting. The battery drains as you listen.",
                area.removeFromBottom (rowHeight), juce::Justification::centredLeft, false);
}
