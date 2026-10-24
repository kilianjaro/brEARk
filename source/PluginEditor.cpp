#include "PluginEditor.h"
#include "ui/Tooltips.h"

namespace
{
using namespace breark;

const char* setupNames[] = { "MONITORS", "HEADPHONES" };
const char* strictnessNames[] = { "GENTLE", "ESCALATING", "HARD" };
} // namespace

//==============================================================================
BrearkEditor::BrearkEditor (BrearkProcessor& p)
    : AudioProcessorEditor (p),
      brearkProcessor (p),
      data (p)
{
    setLookAndFeel (&look);
    auto& brain = brearkProcessor.getBrain();

    for (auto* button : { &setupButton, &modeButton, &soundButton, &sizeButton, &dataButton, &historyButton, &breakButton })
    {
        ui::Look::stylePaperButton (*button);
        addAndMakeVisible (*button);
    }

    // How loud on your ears: a click while music plays anchors that level.
    levelSwitch.setColours (ui::colours::ink, ui::colours::paper);
    levelSwitch.setSelectedOutline (ui::colours::paper);
    levelSwitch.setTooltip (ui::tips::level);
    levelSwitch.onChange = [&brain] (int index) { brain.setLevelNow ((LevelPreset) index); };
    addAndMakeVisible (levelSwitch);

    setupButton.setTooltip (ui::tips::setup);
    modeButton.setTooltip (ui::tips::strictness);
    soundButton.setTooltip (ui::tips::sound);
    sizeButton.setTooltip (ui::tips::size);
    dataButton.setTooltip (ui::tips::data);
    historyButton.setTooltip (ui::tips::history);
    tooltips.setLookAndFeel (&look);

    // Each label cycles through its options; settings are global.
    setupButton.onClick = [&brain]
    {
        auto settings = brain.getSettings();
        settings.setup = (Setup) (((int) settings.setup + 1) % 2);
        brain.setSettings (settings);
    };

    modeButton.onClick = [&brain]
    {
        auto settings = brain.getSettings();
        settings.strictness = (Strictness) (((int) settings.strictness + 1) % 3);
        brain.setSettings (settings);
    };

    soundButton.onClick = [&brain]
    {
        auto settings = brain.getSettings();
        settings.sound = ! settings.sound;
        brain.setSettings (settings);
    };

    sizeButton.onClick = [this]
    {
        brearkProcessor.setPosterSize (! brearkProcessor.isPosterSize());
        applySize();
    };

    breakButton.onClick = [this, &brain]
    {
        if (snapshot.breakPhase == BreakPhase::unplugged)
            brain.snooze();
        else
            brain.toggleBreak();
    };

    historyButton.onClick = [this]
    {
        history.setHistory (brearkProcessor.getBrain().getHistory());
        history.setVisible (true);
    };

    dataButton.onClick = [this] { showData (true); };

    addChildComponent (history);
    history.onClose = [this] { history.setVisible (false); };

    addChildComponent (data);
    data.onBack = [this] { showData (false); };

    timerCallback();
    applySize();
    startTimerHz (4);
}

BrearkEditor::~BrearkEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::String BrearkEditor::getTooltip()
{
    if (data.isVisible() || history.isVisible())
        return {};

    return ui::tooltipAt (model, meterSize(), getMouseXYRelative().toFloat(), *fonts);
}

ui::MeterSize BrearkEditor::meterSize() const noexcept
{
    return brearkProcessor.isPosterSize() ? ui::MeterSize::poster : ui::MeterSize::compact;
}

void BrearkEditor::applySize()
{
    if (data.isVisible())
    {
        setSize (DebugView::width, DebugView::height);
        return;
    }

    history.setVisible (history.isVisible() && meterSize() == ui::MeterSize::poster);
    const auto bounds = ui::meterBounds (meterSize());
    setSize (bounds.getWidth(), bounds.getHeight());
}

void BrearkEditor::showData (bool show)
{
    if (show)
        data.update (snapshot); // the switches show their state right away

    data.setVisible (show);
    applySize();
}

void BrearkEditor::timerCallback()
{
    auto& brain = brearkProcessor.getBrain();
    snapshot = brain.getSnapshot();
    model = ui::makeMeterModel (snapshot, brearkProcessor.getAlertPresenter().getCoachLine(),
                                brearkProcessor.isActiveInstance(), brearkProcessor.getPowerStage());

    const auto settings = brain.getSettings();
    setupButton.setButtonText (setupNames[(int) settings.setup]);
    levelSwitch.setSelectedIndex ((int) settings.preset);
    modeButton.setButtonText (strictnessNames[(int) settings.strictness]);
    soundButton.setButtonText (settings.sound ? "SOUND ON" : "SOUND OFF");
    sizeButton.setButtonText (brearkProcessor.isPosterSize() ? "COMPACT" : "POSTER");
    breakButton.setButtonText (snapshot.breakPhase == BreakPhase::unplugged ? "SNOOZE 10 MIN"
                             : snapshot.breakPhase == BreakPhase::onBreak   ? "END BREAK"
                                                                            : "BREAK");
    breakButton.setTooltip (snapshot.breakPhase == BreakPhase::unplugged ? ui::tips::snooze
                          : snapshot.breakPhase == BreakPhase::onBreak   ? ui::tips::endBreak
                                                                         : ui::tips::breakNow);

    if (history.isVisible())
        history.setHistory (brain.getHistory());

    if (data.isVisible())
        data.update (snapshot);
    else
        repaint();
}

void BrearkEditor::resized()
{
    data.setBounds (getLocalBounds());
    history.setBounds (getLocalBounds());

    const auto poster = meterSize() == ui::MeterSize::poster;
    const auto inset = poster ? 32 : 20;
    const auto rowHeight = poster ? 30 : 24;
    const auto gap = poster ? 8 : 6;

    // Settings, size and DATA: top right.
    auto top = juce::Rectangle<int> (0, poster ? 22 : 14, getWidth() - inset, rowHeight);
    const int widths[] = { poster ? 100 : 84, poster ? 104 : 86, poster ? 96 : 80, poster ? 82 : 66, poster ? 58 : 48 };
    juce::TextButton* row[] = { &setupButton, &modeButton, &soundButton, &sizeButton, &dataButton };

    for (int i = 4; i >= 0; --i)
    {
        row[i]->setBounds (top.removeFromRight (widths[i]));
        top.removeFromRight (gap);
    }

    // HISTORY (POSTER only) and BREAK: bottom right.
    const auto actionHeight = poster ? 40 : 32;
    auto bottom = juce::Rectangle<int> (0, getHeight() - (poster ? 26 : 16) - actionHeight, getWidth() - inset, actionHeight);
    breakButton.setBounds (bottom.removeFromRight (poster ? 150 : 124));

    levelSwitch.setBounds (ui::levelSwitchArea (meterSize()));

    bottom.removeFromRight (poster ? 10 : 8);
    historyButton.setBounds (bottom.removeFromRight (110));
    historyButton.setVisible (poster);
}

void BrearkEditor::paint (juce::Graphics& g)
{
    if (! data.isVisible())
        ui::paintMeter (g, model, meterSize(), *fonts);
}
