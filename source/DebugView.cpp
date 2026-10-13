#include "DebugView.h"
#include "BuildInfo.h"
#include "ui/Tooltips.h"

namespace
{
using namespace breark::ui;

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

const char* levelNames[] = { "QUIET", "NORMAL", "LOUD" };

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

juce::String clock (double seconds)
{
    const auto minutes = juce::roundToInt (seconds / 60.0);
    return juce::String (minutes / 60).paddedLeft ('0', 2) + ":" + juce::String (minutes % 60).paddedLeft ('0', 2);
}

// A forecast in minutes as h:mm, or "-" when nothing plays.
juce::String forecast (double minutes)
{
    if (! std::isfinite (minutes))
        return "-";

    return clock (minutes * 60.0);
}

juce::String minutesSeconds (double seconds)
{
    const auto total = (int) seconds;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}
} // namespace

//==============================================================================
DebugView::DebugView (BrearkProcessor& p)
    : brearkProcessor (p)
{
    setOpaque (true);
    auto& brain = brearkProcessor.getBrain();

    for (auto* control : { &setupSwitch, &presetSwitch, &warpSwitch, &strictnessSwitch })
    {
        control->setColours (colours::ink, colours::paper);
        addAndMakeVisible (*control);
    }

    setupSwitch.onChange = [&brain] (int index)
    {
        auto settings = brain.getSettings();
        settings.setup = (breark::Setup) index;
        brain.setSettings (settings);
    };

    presetSwitch.onChange = [&brain] (int index) { brain.setLevelNow ((breark::LevelPreset) index); };

    strictnessSwitch.onChange = [&brain] (int index)
    {
        auto settings = brain.getSettings();
        settings.strictness = (breark::Strictness) index;
        brain.setSettings (settings);
    };

    warpSwitch.onChange = [&brain] (int index) { brain.setTimeWarp (index == 1 ? 60 : 1); };

    for (auto* button : { &breakButton, &snoozeButton, &testAlertButton, &backButton })
    {
        Look::stylePaperButton (*button);
        addAndMakeVisible (*button);
    }

    setupSwitch.setTooltip (tips::setup);
    presetSwitch.setTooltip (tips::level);
    strictnessSwitch.setTooltip (tips::strictness);
    warpSwitch.setTooltip (tips::timeWarp);
    breakButton.setTooltip (tips::breakNow);
    snoozeButton.setTooltip (tips::snooze);
    testAlertButton.setTooltip (tips::testAlert);
    backButton.setTooltip (tips::back);
    breakButton.onClick = [&brain] { brain.toggleBreak(); };
    snoozeButton.onClick = [&brain] { brain.snooze(); };
    backButton.onClick = [this] { if (onBack != nullptr) onBack(); };

    testAlertButton.onClick = [this]
    {
        breark::Alert alert;
        alert.kind = breark::AlertKind::breakDue;
        alert.withSound = true;
        alert.battery = snapshot.battery;
        alert.breakMinutes = snapshot.suggestedBreakMinutes;
        brearkProcessor.getAlertPresenter().present (alert);
    };

    setSize (width, height);
}

void DebugView::update (const breark::Snapshot& newSnapshot)
{
    snapshot = newSnapshot;

    // Requested settings show immediately; the model follows within a second.
    const auto settings = brearkProcessor.getBrain().getSettings();
    setupSwitch.setSelectedIndex ((int) settings.setup);
    presetSwitch.setSelectedIndex ((int) settings.preset);
    strictnessSwitch.setSelectedIndex ((int) settings.strictness);
    warpSwitch.setSelectedIndex (snapshot.timeWarp > 1 ? 1 : 0);
    breakButton.setButtonText (snapshot.breakPhase == breark::BreakPhase::onBreak ? "END BREAK" : "BREAK");
    breakButton.setTooltip (snapshot.breakPhase == breark::BreakPhase::onBreak ? tips::endBreak : tips::breakNow);
    snoozeButton.setEnabled (snapshot.breakPhase == breark::BreakPhase::unplugged);
    repaint();
}

void DebugView::resized()
{
    backButton.setBounds (getWidth() - 24 - 90, 24, 90, 30);

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

void DebugView::paint (juce::Graphics& g)
{
    g.fillAll (snapshot.timeWarp > 1 ? colours::paper.darker (0.08f) : colours::paper);
    g.setColour (colours::ink);

    auto area = getLocalBounds().reduced (24);

    g.setFont (fonts->grotesk (64.0f));
    g.drawText ("DATA.", area.removeFromTop (80), juce::Justification::topLeft, false);

    g.fillRect (area.removeFromTop (3));
    area.removeFromTop (14);

    const auto& s = snapshot;
    const auto& f = s.factors;
    const auto micro = juce::String::fromUTF8 ("\xc2\xb5s");
    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (brearkProcessor.wrapperType));

    const auto status = ! brearkProcessor.isActiveInstance() ? juce::String ("INACTIVE - ANOTHER brEARk IS LISTENING")
                      : s.hostOff                           ? juce::String ("OFF - SWITCHED OFF BY THE HOST, FROZEN")
                      : s.audioStalled                      ? juce::String ("NO AUDIO FROM THE HOST - SILENCE")
                      : s.rendering                         ? juce::String ("EXPORT DETECTED - NOT COUNTING")
                      : s.recovering                        ? juce::String ("RECOVERING")
                      : s.silent                            ? juce::String ("SILENCE")
                                                            : juce::String ("LISTENING");

    const auto transport = juce::String (s.transportRecording ? "RECORDING" : s.transportPlaying ? "PLAYING" : "STOPPED");

    const juce::StringArray rows {
        "STATUS        " + status + "   (" + transport + ")",
        "LOUDNESS      M " + db (s.lufsMomentary) + "   S " + db (s.lufsShortTerm) + " LUFS   ANCHOR " + db (s.referenceLufs)
            + " LUFS (" + levelNames[(int) s.settings.preset] + (s.levelAnchored ? ", SET)" : ", LEARNING)"),
        "VS ANCHOR     " + (s.silent ? juce::String ("-") : signedDb (s.signalVsAnchorLu) + " LU") + "  (LAST 2 MIN OF PLAY)",
        "LEVEL (A)     " + db (s.levelA) + " dBFS(A)   PEAK " + db (s.peakShortTerm) + " dBFS",
        "",
        "HARSHNESS     PRESENCE " + db (s.presenceToBody) + " dB   HIGHS " + db (s.highsToBody) + " dB  (VS 250-2K)   " + times (f.harshness),
        "DYNAMICS      PSR " + db (s.psr) + " dB   " + times (f.dynamics),
        "REPETITION    BAR PLAYS " + juce::String (s.barPlays, 1) + "   LOOPING " + minutesSeconds (s.loopingSeconds)
            + (s.loopBars > 0 ? " (" + juce::String (s.loopBars) + "-BAR LOOP)" : juce::String())
            + "   STRAIN " + juce::String (juce::roundToInt (s.loopStrain)) + " / " + juce::String (juce::roundToInt (breark::model::loopHintStrainMinutes))
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
        "HEARING DOSE  " + juce::String (s.doseToday, 1) + " % TODAY   " + juce::String (s.doseWeek, 1) + " % WEEK  (WHO)",
        "FORECAST      " + forecast (s.minutesToBreak) + " TO EMPTY   " + forecast (s.minutesToBreakQuieter) + " ONE LEVEL QUIETER   DOSE USED UP IN "
            + forecast (s.minutesToFullDose) + (s.dayDone ? "   DAY DONE" : "") + (s.unpluggedForDay ? " (UNPLUGGED)" : ""),
        "TODAY         " + clock (s.listeningSecondsToday) + " LISTENED   " + clock (s.secondsSinceBreak)
            + " SINCE BREAK   " + juce::String (s.breaksToday) + " BREAKS   LONGEST " + clock (s.longestStretchToday)
            + "   EMPTY " + juce::String (s.emptyBatteryToday) + "x   NEXT BREAK " + juce::String (juce::roundToInt (s.suggestedBreakMinutes)) + " MIN",
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
            + "  " + (brearkProcessor.isUsingDoublePrecision() ? "64" : "32") + "-BIT  FFT " + juce::String (s.fftSize),
        "AUDIO THREAD  " + juce::String (s.audioMicrosAverage, 2) + " " + micro + " AVG   "
            + juce::String (s.audioMicrosMax, 2) + " " + micro + " MAX  / BLOCK",
        "WORKER        " + juce::String (s.workerLoadPercent, 3) + " % OF ONE CORE   "
            + juce::String (s.samplesDropped) + " DROPPED   WAKE " + juce::String (s.workerWakeMsAverage, 0) + " MS",
        "EXPORTS       " + juce::String (s.rendersSeen) + " SEEN   HOST FLAG " + yesNo (s.lastRenderHadHostFlag)
            + "   SPEED " + juce::String (s.renderSpeed, 2) + "x",
    };

    g.setFont (fonts->mono (11.5f));
    rowsTop = area.getY();
    rowLabels.clearQuick();

    for (const auto& row : rows)
    {
        rowLabels.add (row.substring (0, 14).trim());
        g.drawText (row, area.removeFromTop (rowHeight), juce::Justification::centredLeft, false);
    }

    g.drawText ("> " + brearkProcessor.getAlertPresenter().getCoachLine(),
                area.removeFromBottom (rowHeight), juce::Justification::centredLeft, false);

    g.setColour (colours::ink.withAlpha (0.6f));
    g.setFont (fonts->mono (10.0f));
    g.drawText ("BUILD " BREARK_BUILD_ID, juce::Rectangle<int> (24, 62, getWidth() - 48, 14), juce::Justification::centredRight, false);
}

juce::String DebugView::getTooltip()
{
    const auto y = getMouseXYRelative().y - rowsTop;
    return y >= 0 && juce::isPositiveAndBelow (y / rowHeight, rowLabels.size()) ? tips::dataRow (rowLabels[y / rowHeight]) : juce::String();
}
