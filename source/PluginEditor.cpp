#include "PluginEditor.h"
#include "BuildInfo.h"

namespace
{
const auto paper = juce::Colour (0xffededed);
const auto ink   = juce::Colour (0xff0a0a0a);

constexpr int rowHeight = 19;

// Values below −100 dB are numerical decay, not signal.
juce::String db (float value, int decimals = 1)
{
    if (! std::isfinite (value) || value <= -100.0f)
        return "-inf";

    // JUCE treats 0 decimal places as "as many as needed".
    return decimals == 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);
}

juce::String signedDb (float value)
{
    return std::isfinite (value) ? (value >= 0.0f ? "+" : "") + juce::String (value, 1) : juce::String ("-");
}

juce::String times (double factor)
{
    return "x" + juce::String (factor, 2);
}

juce::String phaseName (breark::BreakPhase phase)
{
    switch (phase)
    {
        case breark::BreakPhase::listening: return "LISTENING";
        case breark::BreakPhase::warned:    return "HEADS UP GIVEN (20 %)";
        case breark::BreakPhase::due:       return "BREAK DUE";
        case breark::BreakPhase::onBreak:   return "ON A BREAK";
        case breark::BreakPhase::unplugged: return "UNPLUGGED";
    }

    return {};
}

juce::String stageName (breark::PowerDown::Stage stage)
{
    switch (stage)
    {
        case breark::PowerDown::Stage::run:          return "RUN (UNTOUCHED)";
        case breark::PowerDown::Stage::spinningDown: return "SPINNING DOWN";
        case breark::PowerDown::Stage::stopped:      return "MUTED";
        case breark::PowerDown::Stage::spinningUp:   return "SPINNING UP";
    }

    return {};
}

juce::String yesNo (bool value)
{
    return value ? "YES" : "NO";
}

// Hours:minutes, for durations measured in hours.
juce::String clock (double seconds)
{
    const auto minutes = juce::roundToInt (seconds / 60.0);
    return juce::String (minutes / 60).paddedLeft ('0', 2) + ":" + juce::String (minutes % 60).paddedLeft ('0', 2);
}

// Minutes:seconds, for loops.
juce::String minutesSeconds (double seconds)
{
    const auto total = (int) seconds;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}
} // namespace

//==============================================================================
BrearkEditor::BrearkEditor (BrearkProcessor& p)
    : AudioProcessorEditor (p),
      brearkProcessor (p)
{
    auto& brain = brearkProcessor.getBrain();

    for (auto* control : { &setupSwitch, &presetSwitch, &warpSwitch })
    {
        control->setColours (ink, paper);
        addAndMakeVisible (*control);
    }

    setupSwitch.onChange = [&brain] (int index)
    {
        auto settings = brain.getSettings();
        settings.setup = (breark::Setup) index;
        brain.setSettings (settings);
    };

    presetSwitch.onChange = [&brain] (int index)
    {
        auto settings = brain.getSettings();
        settings.preset = (breark::LevelPreset) index;
        brain.setSettings (settings);
    };

    warpSwitch.onChange = [&brain] (int index) { brain.setTimeWarp (index == 1 ? 60 : 1); };

    strictnessSwitch.setColours (ink, paper);
    addAndMakeVisible (strictnessSwitch);
    strictnessSwitch.onChange = [&brain] (int index)
    {
        auto settings = brain.getSettings();
        settings.strictness = (breark::Strictness) index;
        brain.setSettings (settings);
    };

    for (auto* button : { &breakButton, &snoozeButton, &testAlertButton })
    {
        button->setColour (juce::TextButton::buttonColourId, ink);
        button->setColour (juce::TextButton::textColourOffId, paper);
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (*button);
    }

    breakButton.onClick = [&brain] { brain.toggleBreak(); };
    snoozeButton.onClick = [&brain] { brain.snooze(); };

    testAlertButton.onClick = [this]
    {
        breark::Alert alert;
        alert.kind = breark::AlertKind::breakDue;
        alert.withSound = true;
        alert.battery = snapshot.battery;
        alert.breakMinutes = snapshot.suggestedBreakMinutes;
        brearkProcessor.getAlertPresenter().present (alert);
    };

    setSize (720, 820);
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

    // Requested settings show immediately; the model follows within a second.
    const auto settings = brearkProcessor.getBrain().getSettings();
    setupSwitch.setSelectedIndex ((int) settings.setup);
    presetSwitch.setSelectedIndex ((int) settings.preset);
    strictnessSwitch.setSelectedIndex ((int) settings.strictness);
    warpSwitch.setSelectedIndex (snapshot.timeWarp > 1 ? 1 : 0);
    breakButton.setButtonText (snapshot.breakPhase == breark::BreakPhase::onBreak ? "END BREAK" : "BREAK");
    snoozeButton.setEnabled (snapshot.breakPhase == breark::BreakPhase::unplugged);

    repaint();
}

void BrearkEditor::resized()
{
    auto controls = getLocalBounds().reduced (24).removeFromBottom (rowHeight + 70);
    auto top = controls.removeFromTop (28);
    controls.removeFromTop (8);
    auto bottom = controls.removeFromTop (28);

    setupSwitch.setBounds (top.removeFromLeft (220));
    top.removeFromLeft (12);
    presetSwitch.setBounds (top.removeFromLeft (260));
    top.removeFromLeft (12);
    warpSwitch.setBounds (top);

    strictnessSwitch.setBounds (bottom.removeFromLeft (330));
    bottom.removeFromLeft (12);
    breakButton.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (8);
    snoozeButton.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (8);
    testAlertButton.setBounds (bottom);
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
    const auto& f = s.factors;
    const auto micro = juce::String::fromUTF8 ("\xc2\xb5s");
    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));

    const auto status = ! brearkProcessor.isActiveInstance() ? juce::String ("INACTIVE - ANOTHER brEARk IS LISTENING")
                      : s.rendering                         ? juce::String ("EXPORT DETECTED - NOT COUNTING")
                      : s.recovering                        ? juce::String ("RECOVERING")
                      : s.silent                            ? juce::String ("SILENCE")
                                                            : juce::String ("LISTENING");

    const auto transport = juce::String (s.transportRecording ? "RECORDING" : s.transportPlaying ? "PLAYING" : "STOPPED");

    const juce::StringArray rows {
        "STATUS        " + status + "   (" + transport + ")",
        "LOUDNESS      M " + db (s.lufsMomentary) + "   S " + db (s.lufsShortTerm) + " LUFS   REF " + db (s.referenceLufs),
        "LEVEL (A)     " + db (s.levelA) + " dBFS(A)   PEAK " + db (s.peakShortTerm) + " dBFS",
        "",
        "HARSHNESS     PRESENCE " + db (s.presenceToBody) + " dB   HIGHS " + db (s.highsToBody) + " dB  (VS 250-2K)   " + times (f.harshness),
        "DYNAMICS      PSR " + db (s.psr) + " dB   " + times (f.dynamics),
        "REPETITION    BAR PLAYS " + juce::String (s.barPlays, 1) + "   LOOPING " + minutesSeconds (s.loopingSeconds)
            + (s.loopBars > 0 ? " (" + juce::String (s.loopBars) + "-BAR LOOP)" : juce::String())
            + "   " + times (f.repetition),
        "LOOP CHECK    " + (s.loopHasGrid ? "RHYTHM " + juce::String (s.loopBestRhythm, 2) + "  SHIFT " + juce::String (s.loopBestShiftDb, 1)
                                               + " dB @ " + juce::String (s.loopBestBars) + " BARS   (LOOP: RHYTHM >= "
                                               + juce::String (breark::model::loopMinRhythm, 2) + ", SHIFT <= "
                                               + juce::String (breark::model::loopMaxShiftDb, 1) + " dB)"
                                             : juce::String ("NO TEMPO GRID (TRANSPORT STOPPED)")),
        "NIGHT         " + juce::Time::getCurrentTime().formatted ("%H:%M") + "   " + times (f.night),
        "DRAIN         " + times (s.drainFactor) + "  (FACTORS x DAILY LOAD, MAX " + times (breark::model::maxCombinedFactor) + ")   CREEP " + signedDb (s.levelCreepDb) + " dB / 1 H",
        "",
        "EAR BATTERY   " + juce::String (s.battery, 1) + " %",
        "DOSE          " + juce::String (s.doseToday, 1) + " % TODAY   " + juce::String (s.doseWeek, 1) + " % WEEK  (WHO)",
        "EST. LEVEL    ~" + db (s.perceivedSpl, 0) + " dB PERCEIVED   ~" + db (s.doseSpl, 0) + " dB(A) DOSE",
        "TODAY         " + clock (s.listeningSecondsToday) + " LISTENED   " + clock (s.secondsSinceBreak)
            + " SINCE BREAK   " + juce::String (s.breaksToday) + " BREAKS   NEXT BREAK " + juce::String (juce::roundToInt (s.suggestedBreakMinutes)) + " MIN",
        "BREAKS        " + phaseName (s.breakPhase)
            + (s.breakPhase == breark::BreakPhase::onBreak ? "   " + minutesSeconds (s.breakSecondsLeft) + " LEFT" : juce::String())
            + (s.breakPhase == breark::BreakPhase::due || s.breakPhase == breark::BreakPhase::unplugged
                   ? "   " + minutesSeconds (s.secondsSinceDue) + " LISTENED SINCE EMPTY" : juce::String()),
        "POWER         " + stageName (brearkProcessor.getPowerStage())
            + (s.breakPhase == breark::BreakPhase::unplugged ? "   " + minutesSeconds (s.unpluggedSeconds) + " OF 5:00" : juce::String())
            + (s.snoozeSecondsLeft > 0.0 ? "   SNOOZE " + minutesSeconds (s.snoozeSecondsLeft) + " LEFT" : juce::String())
            + "   TODAY " + juce::String (s.powerDownsToday) + " POWER-DOWNS, " + juce::String (s.snoozesToday) + " SNOOZES",
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

    g.drawText ("> " + brearkProcessor.getAlertPresenter().getCoachLine(),
                area.removeFromBottom (rowHeight), juce::Justification::centredLeft, false);

    g.setColour (ink.withAlpha (0.5f));
    g.drawText ("BUILD " BREARK_BUILD_ID, getLocalBounds().reduced (24, 8).removeFromTop (16),
                juce::Justification::topRight, false);
}
