// brEARk UI renderer
// ------------------
// Renders the meter view (every state, COMPACT and POSTER), the history view
// and the pop-ups to PNG files at 2× resolution, through the same model and
// paint code the plugin uses. For checking the design without opening Live.
//
// Usage: brEARk_RenderUI [output folder]

#include "PluginEditor.h"
#include "ui/HistoryView.h"
#include "ui/MeterView.h"
#include "ui/ToastWindow.h"

#include <iostream>

using namespace breark;

namespace
{
constexpr float scale = 2.0f;

struct Sample
{
    const char* name;
    std::function<void (Snapshot&)> setUp;
    const char* coach;
    PowerDown::Stage stage = PowerDown::Stage::run;
    bool active = true;
};

Snapshot listening()
{
    Snapshot s;
    s.silent = false;
    s.lufsShortTerm = -8.7f;
    s.perceivedSpl = 81.0f;
    s.psr = 8.4f;
    s.doseToday = 18.0f;
    s.doseWeek = 9.0f;
    s.listeningSecondsToday = 65.0 * 60.0;
    s.secondsSinceBreak = 41.0 * 60.0;
    s.drainFactor = 1.21f;
    s.factors = { 1.03, 1.16, 1.0, 1.0 };
    s.suggestedBreakMinutes = 12.0f;
    s.minutesToBreak = 42.0;
    s.minutesToBreakQuieter = 97.0;
    s.minutesToFullDose = 262.0;
    s.minutesToRecover = 9.0;
    return s;
}

void save (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
}

template <typename Painter>
juce::Image render (int width, int height, Painter&& paint)
{
    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) width * scale), juce::roundToInt ((float) height * scale), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));
    paint (g);
    return image;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::SharedResourcePointer<ui::Fonts> fonts;

    const juce::File folder (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "ui-renders"));
    folder.createDirectory();

    const std::vector<Sample> samples {
        { "1-fresh",     [] (Snapshot& s) { s.battery = 92.0f; s.doseToday = 4.0f; }, "Fresh ears. Make the big decisions now." },
        { "2-ok",        [] (Snapshot& s) { s.battery = 64.0f; }, "Good level, good ears, good time to decide." },
        { "3-tiring",    [] (Snapshot& s) { s.battery = 31.0f; s.loopBars = 8; s.loopingSeconds = 252.0; s.factors.repetition = 1.17; s.drainFactor = 1.49f; },
                         "Battery at 31 %. Fine-tuning is for later; finish the structure now." },
        { "4-low",       [] (Snapshot& s) { s.battery = 14.0f; s.breakPhase = BreakPhase::warned; }, "14 % left. Finish the thought, then plan a break." },
        { "5-break",     [] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.secondsSinceDue = 120.0; },
                         "Take a break of 12 minutes and take care of your ears." },
        { "6-stop",      [] (Snapshot& s) { s.battery = 0.0f; s.breakPhase = BreakPhase::due; s.secondsSinceDue = 1220.0; }, "STOP. 12 MIN. NO AUDIO." },
        { "7-resting",   [] (Snapshot& s) { s.battery = 58.0f; s.silent = true; s.breakPhase = BreakPhase::onBreak; s.breakSecondsLeft = 432.0; s.lufsShortTerm = Snapshot::minusInfinity; s.perceivedSpl = Snapshot::minusInfinity; },
                         "Hum the hook. Does it still stand without the production?" },
        { "8-unplugged", [] (Snapshot& s) { s.battery = 21.0f; s.breakPhase = BreakPhase::unplugged; s.unpluggedSeconds = 190.0; s.silent = true; s.minutesToRecover = 9.0; },
                         "SILENCE IS NOT A PUNISHMENT. IT'S MAINTENANCE.", PowerDown::Stage::stopped },
        { "9-inactive",  [] (Snapshot& s) { s.battery = 64.0f; }, "Listening.", PowerDown::Stage::run, false },
        { "10-off",      [] (Snapshot& s) { s.battery = 47.0f; s.hostOff = true; }, "Listening." },
        { "12-done",     [] (Snapshot& s) { s.battery = 46.0f; s.doseToday = 104.0f; s.dayDone = true; }, "Switch to editing, arranging, anything quiet. Tomorrow you'll hear it fresh." },
        { "13-done-unplugged", [] (Snapshot& s) { s.battery = 46.0f; s.doseToday = 104.0f; s.dayDone = true; s.unpluggedForDay = true; s.breakPhase = BreakPhase::unplugged; s.silent = true; s.minutesToRecover = 9.0; },
                         "SAVE THE SET. WRITE DOWN WHAT'S NEXT. GO.", PowerDown::Stage::stopped },
        { "14-anchor",   [] (Snapshot& s) { s.battery = 64.0f; s.levelAnchored = true; s.signalVsAnchorLu = 4.2f; }, "Listening." },
        { "11-long-coach", [] (Snapshot& s) { s.battery = 52.0f; s.loopBars = 4; s.loopingSeconds = 3700.0; s.loopStrain = 61.0f; s.factors.repetition = 1.2; s.drainFactor = 1.35f; },
                         "After 1:01:40 on one loop the ear stops hearing what never changes. Play it in context once." },
    };

    for (const auto& sample : samples)
    {
        auto snapshot = listening();
        sample.setUp (snapshot);
        const auto model = ui::makeMeterModel (snapshot, sample.coach, sample.active, sample.stage);

        for (const auto size : { ui::MeterSize::compact, ui::MeterSize::poster })
        {
            const auto bounds = ui::meterBounds (size);
            const auto name = juce::String (sample.name) + (size == ui::MeterSize::compact ? "-compact.png" : "-poster.png");
            save (render (bounds.getWidth(), bounds.getHeight(), [&] (juce::Graphics& g) { ui::paintMeter (g, model, size, *fonts); }),
                  folder.getChildFile (name));
        }
    }

    // History: a morning session with a break, and a week with one heavy day.
    Brain::History history;
    juce::Random random (3);
    auto battery = 100.0;

    for (int minute = 270; minute <= 480; ++minute) // 09:30 – 13:00, switched off 11:40 – 12:05
    {
        const auto off = minute >= 400 && minute < 425;

        if (! off)
            battery += (minute > 360 && minute < 375) ? (100.0 - battery) * 0.18 : -0.38 - 0.1 * random.nextDouble();

        history.today.push_back ({ minute, juce::jlimit (0, 100, juce::roundToInt (battery)), off });
    }

    history.nowMinute = 480;
    history.thisWeek = { 1, 3, 41.0, 0, 6.5 * 3600.0, 1.4 * 3600.0, 7, 2, 0, 0 };
    history.lastWeek = { 0, 5, 82.0, 1, 14.3 * 3600.0, 1.7 * 3600.0, 9, 4, 1, 1 };

    for (auto [day, dose] : std::vector<std::pair<int, double>> { { 20732, 34.0 }, { 20733, 61.0 }, { 20735, 48.0 }, { 20736, 112.0 }, { 20737, 22.0 }, { 20738, 41.0 } })
        history.lastDays.push_back ({ day, dose, 0.0, 0, 0, 0, day == 20735 ? 80.0 * 60.0 : (day == 20738 ? 25.0 * 60.0 : 0.0) });

    save (render (960, 600, [&] (juce::Graphics& g) { ui::paintHistory (g, { 0.0f, 0.0f, 960.0f, 600.0f }, history, *fonts); }),
          folder.getChildFile ("history.png"));

    // Pop-ups.
    const std::vector<std::pair<const char*, ToastContent>> toasts {
        { "toast-break", { "BREAK.", "Take a break of 12 minutes and take care of your ears.", "EAR BATTERY 0 %   BREAK 12 MIN", false, true } },
        { "toast-stop", { "STOP.", "NOTHING YOU DO NOW WILL SURVIVE TOMORROW.", "EAR BATTERY 0 %   BREAK 12 MIN", true, true } },
        { "toast-unplugged", { "UNPLUGGED.", "THE TURNTABLE STOPPED. SO DO YOU.", "EAR BATTERY 0 %", true, false, true } },
        { "toast-week", { "LAST WEEK.", "82 % of the WHO weekly dose, 1 day over the daily dose. 14:20 listened on 5 days, longest stretch 1:40. Battery ran empty 4 times, 9 breaks.",
                          "DETAILS IN HISTORY (POSTER)", false, false } },
        { "toast-checkin", { "HOW LOUD?", "Your music is 7 LU louder than when you set NORMAL. How loud is it on your ears now?", "", false, false, false, true } },
    };

    for (const auto& [name, content] : toasts)
        save (render (ToastWindow::width, ToastWindow::height,
                      [&] (juce::Graphics& g) { paintToast (g, { 0.0f, 0.0f, (float) ToastWindow::width, (float) ToastWindow::height }, content, *fonts); }),
              folder.getChildFile (juce::String (name) + ".png"));

    // The check-in as a real window (with its QUIET / NORMAL / LOUD answers).
    {
        ToastWindow toast;
        ToastContent content { "HOW LOUD?", "Your music is 7 LU louder than when you set NORMAL. How loud is it on your ears now?", "", false, false, false, true };
        toast.setVisible (false);
        toast.show (content);
        save (toast.createComponentSnapshot (toast.getLocalBounds(), true, scale), folder.getChildFile ("toast-checkin-window.png"));

        // The session check-in (M11.1): where you listen, and how loud.
        toast.show ({ "NEW SESSION.", "Where do you listen, and how loud is it on your ears?", "", false, false, false, true, true, 0 });
        save (toast.createComponentSnapshot (toast.getLocalBounds(), true, scale), folder.getChildFile ("toast-session-window.png"));
        toast.dismiss();
    }

    // Tooltips, drawn where the pointer would be (POSTER, a factor and a stat).
    {
        ui::Look look;
        auto snapshot = listening();
        snapshot.battery = 58.0f;
        snapshot.drainFactor = 1.21f;
        snapshot.factors = { 1.03, 1.16, 1.0, 1.0 };
        snapshot.psr = 8.4f;
        const auto model = ui::makeMeterModel (snapshot, "Fine-tune later; finish the structure now.", true, PowerDown::Stage::run);

        for (const auto& [name, pointer] : std::vector<std::pair<const char*, juce::Point<int>>> { { "tooltip-factor", { 330, 494 } }, { "tooltip-stat", { 700, 430 } } })
        {
            const auto text = ui::tooltipAt (model, ui::MeterSize::poster, pointer.toFloat(), *fonts);
            const auto box = look.getTooltipBounds (text, pointer, { 0, 0, 960, 600 });

            save (render (960, 600, [&] (juce::Graphics& g)
                  {
                      ui::paintMeter (g, model, ui::MeterSize::poster, *fonts);
                      const juce::Graphics::ScopedSaveState state (g);
                      g.setOrigin (box.getPosition());
                      look.drawTooltip (g, text, box.getWidth(), box.getHeight());
                  }),
                  folder.getChildFile (juce::String (name) + ".png"));
        }
    }

    // The real editor, buttons and all, as the plugin shows it (fresh state).
    {
        const auto stateFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("brEARk-render-state", ".json");
        setenv ("BREARK_STATE_FILE", stateFile.getFullPathName().toRawUTF8(), 1);
        const juce::ScopeGuard removeStateFile { [&] { stateFile.deleteFile(); } };

        BrearkProcessor processor;

        auto click = [] (juce::Component& parent, const juce::String& text)
        {
            // onClick directly: triggerClick() would only post a message.
            for (auto* child : parent.getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == text && button->onClick)
                    button->onClick();
        };

        auto snapshot = [&] (juce::Component& editor, const juce::String& name)
        {
            save (editor.createComponentSnapshot (editor.getLocalBounds(), true, scale), folder.getChildFile (name));
        };

        for (const auto poster : { false, true })
        {
            processor.setPosterSize (poster);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            snapshot (*editor, poster ? "editor-poster.png" : "editor-compact.png");

            if (poster)
            {
                click (*editor, "HISTORY");
                snapshot (*editor, "editor-poster-history.png");
            }
            else
            {
                click (*editor, "DATA");
                snapshot (*editor, "editor-data.png");
            }
        }
    }

    std::cout << "Rendered to " << folder.getFullPathName() << "\n";
    return 0;
}
