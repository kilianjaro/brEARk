#include "PluginEditor.h"

namespace
{
const auto paper = juce::Colour (0xffededed);
const auto ink   = juce::Colour (0xff0a0a0a);

juce::String db (float value, int decimals = 1)
{
    return std::isfinite (value) ? juce::String (value, decimals) : juce::String ("-inf");
}

juce::String yesNo (bool value)
{
    return value ? "YES" : "NO";
}
} // namespace

//==============================================================================
BrearkEditor::BrearkEditor (BrearkProcessor& p)
    : AudioProcessorEditor (p),
      brearkProcessor (p)
{
    setSize (520, 470);
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
    repaint();
}

void BrearkEditor::paint (juce::Graphics& g)
{
    g.fillAll (paper);
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
                      : s.silent                            ? juce::String ("SILENCE")
                                                            : juce::String ("LISTENING");

    const juce::StringArray rows {
        "STATUS        " + status,
        "LOUDNESS      M " + db (s.lufsMomentary) + "   S " + db (s.lufsShortTerm) + " LUFS",
        "LEVEL (A)     " + db (s.levelA) + " dBFS(A)",
        "PEAK / PLR    " + db (s.peakShortTerm) + " dBFS   PLR " + db (s.peakShortTerm - s.lufsShortTerm) + " dB",
        "SPECTRUM      PRESENCE " + db (s.presence) + " dB   HIGHS " + db (s.highs) + " dB",
        "",
        "ENGINE        " + juce::String (s.sampleRate, 0) + " HZ  " + wrapper.toUpperCase()
            + "  " + (processor.isUsingDoublePrecision() ? "64" : "32") + "-BIT  FFT " + juce::String (s.fftSize),
        "AUDIO THREAD  " + juce::String (s.audioMicrosAverage, 2) + " " + micro + " AVG   "
            + juce::String (s.audioMicrosMax, 2) + " " + micro + " MAX  / BLOCK",
        "WORKER        " + juce::String (s.workerLoadPercent, 3) + " % OF ONE CORE   "
            + juce::String (s.samplesDropped) + " DROPPED",
        "WORKER WAKE   " + juce::String (s.workerWakeMsAverage, 0) + " MS AVG   "
            + juce::String (s.workerWakeMsMax, 0) + " MS MAX",
        "EXPORTS       " + juce::String (s.rendersSeen) + " SEEN   HOST FLAG " + yesNo (s.lastRenderHadHostFlag)
            + "   SPEED " + juce::String (s.renderSpeed, 2) + "x",
    };

    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

    for (const auto& row : rows)
        g.drawText (row, area.removeFromTop (19), juce::Justification::centredLeft, false);

    g.drawText ("> M1. Listening, measuring, not judging yet.",
                area.removeFromBottom (19), juce::Justification::centredLeft, false);
}
