#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// Hover explanations (M9). Each has two parts, separated by a line break:
// what it shows (technical), then what it means for you. The tooltip draws
// the first part bold. Plain ASCII where possible: JUCE reads C++ literals as
// Latin-1, so anything else goes through juce::String::fromUTF8.
namespace breark::ui::tips
{
inline juce::String tip (const char* shows, const char* means)
{
    return juce::String::fromUTF8 (shows) + "\n" + juce::String::fromUTF8 (means);
}

//── Main window: state ────────────────────────────────────────────────────────
inline juce::String word (const juce::String& w)
{
    if (w == "BREAK.")     return tip ("The ear battery is empty: a break is due.", "After 15 more minutes of listening it turns into STOP. Silence pauses that clock.");
    if (w == "STOP.")      return tip ("The break has been due for over 15 minutes of listening.", "Tired ears misjudge level, highs and balance. Take the break, the mix will wait.");
    if (w == "RESTING.")   return tip ("Silence for over a minute, or a break you started: your ears recover.", "The battery refills (63 % of the gap in 5 min). Playing again ends it.");
    if (w == "UNPLUGGED.") return tip ("HARD mode pulled the plug: the output is muted for at least 5 minutes.", "SNOOZE buys 10 minutes. Leaving HARD mode plugs back in at once.");
    if (w == "OFF.")       return tip ("Live has switched brEARk off: nothing is measured.", "Nothing drains or recovers while it's off. HISTORY shows the time as OFF.");
    if (w == "INACTIVE.")  return tip ("Another brEARk in this Live set is already listening; this one does nothing.", "One brEARk per set, on the master, so nothing is counted twice.");
    if (w == "DONE.")      return tip ("Today's WHO sound dose is full (100 %). In HARD mode the plug stays out until 05:00.", "More listening today is borrowed from your future ears. Quiet work, or call it a day.");

    return tip ("Your ear battery in one word: FRESH above 70 %, OK above 40 %, TIRING below.",
                "Below 20 % the black turns red; at 0 % it's time for a break.");
}

inline const juce::String battery = tip ("EAR BATTERY: listening fatigue from your loudness, time and the drain reasons.",
                                         "It drains while you listen and refills in silence. 0 % means your ears need a break.");

inline const juce::String status = tip ("Details of the current state, in minutes:seconds.",
                                        "Exports are never counted; TEST MODE is never saved.");

//── Main window: numbers ──────────────────────────────────────────────────────
inline const juce::String doseToday = tip ("Today's sound dose, as a share of the WHO daily allowance (8 hours at 80 dB(A), A-weighted).",
                                           "It never recovers during the day: 100 % is the most your ears should take today. New day at 05:00.");

inline const juce::String doseWeek = tip ("The last 7 days' dose, as a share of the WHO weekly allowance (40 hours at 80 dB(A)).",
                                          "Above 100 % the risk of lasting hearing damage rises. Plan quieter days.");

inline const juce::String listened = tip ("Time with sound playing today (hours:minutes), since 05:00.",
                                          "Silence and exports don't count.");

inline const juce::String sinceBreak = tip ("Listening time since your last break of 5 minutes or more (hours:minutes).",
                                            "After about an hour, ears and judgment tire, even at moderate levels.");

inline const juce::String level = tip ("Estimated level at your listening position, from the loudness and your level preset.",
                                       "Only an estimate: brEARk can't see your volume knob. Wrong? Use NOT THAT LOUD in DATA.");

inline const juce::String loudness = tip ("Short-term loudness of the master in LUFS, over 3 seconds (ITU-R BS.1770), as Live's meters measure it.",
                                          "What brEARk hears, before your monitor volume.");

//── Main window: why the battery drains ───────────────────────────────────────
inline const juce::String drain = tip ("How fast the battery drains, compared with plain listening at your normal level.",
                                       "The reasons follow; together they are capped at +60 %.");

inline juce::String harsh (float presenceDb, float highsDb)
{
    const auto now = std::isfinite (presenceDb) && std::isfinite (highsDb)
                       ? "2-5 kHz at " + juce::String (presenceDb, 1) + " dB and 8-16 kHz at " + juce::String (highsDb, 1) + " dB"
                       : juce::String ("not measured yet");
    return tip (("HARSH HIGHS: the presence and air bands against the 250 Hz-2 kHz body, over 10 s (" + now + ").").toRawUTF8(),
                "A bright, edgy sound tires the ears faster: up to +30 %.");
}

inline juce::String dynamics (float psr)
{
    const auto now = std::isfinite (psr) ? "now " + juce::String (psr, 1) + " dB" : juce::String ("not measured yet");
    return tip (("LITTLE DYNAMICS: PSR, the gap between peaks and loudness (" + now + "). 14 dB or more is dynamic, 7 dB or less squashed.").toRawUTF8(),
                "Dense, limited sound gives the ears no rest: up to +20 %.");
}

inline juce::String loop (int bars, double loopingSeconds, float strainMinutes)
{
    const auto now = bars > 0 ? juce::String (bars) + " bars for " + juce::String ((int) loopingSeconds / 60) + " min"
                              : juce::String ("no loop right now");
    return tip (("SAME LOOP: " + now + ". Strain " + juce::String (juce::roundToInt (strainMinutes))
                 + " = minutes of a 4-bar loop; each play weighs log2 (1 + loop seconds).").toRawUTF8(),
                "Repetition dulls the ears slowly: up to +20 % at 45. The LOOP pop-up comes at 60.");
}

inline const juce::String lateHour = tip ("LATE HOUR: from 22:00 on, full from 01:00 until the day starts at 05:00.",
                                          "Tired ears drain faster at night: up to +25 %.");

inline const juce::String todaysDose = tip ("TODAY'S DOSE: how much of the WHO daily dose you've used.",
                                            "Ears that already had a full day tire faster: up to +50 % at 100 %.");

//── Main window: time left (M10) ──────────────────────────────────────────────
inline const juce::String timeLeft = tip ("Time until the ear battery is empty, if the level and the drain stay as in the last 2 minutes.",
                                          "Plan the next break, or turn down to stretch the time.");

inline const juce::String quieter = tip ("The same forecast at 6 dB less, about a quarter of the sound energy.",
                                         "Turning down buys time. Tell brEARk with the VOLUME buttons; it can't see your monitor knob.");

inline const juce::String doseLeft = tip ("Time until today's WHO dose is full at the level of the last 2 minutes.",
                                          "After that brEARk says DONE. Every 3 dB quieter doubles this time.");

inline const juce::String recover = tip ("How long silence takes to bring the battery back to 90 %, the end of a suggested break.",
                                         "Recovery starts after 60 s of silence; the BREAK button starts it at once.");

inline const juce::String dayDone = tip ("Today's WHO dose is full: 8 hours' worth at 80 dB(A).",
                                         "Quiet work from here, or call it a day. A new day starts at 05:00.");

inline const juce::String volumeSet = tip ("How far you told brEARk you turned your monitors down or up.",
                                           "Level, time left and dose follow it. NOT THAT LOUD or the next day resets it.");

inline const juce::String volumeButtons = tip ("Tell brEARk you turned your monitors down 6 or 3 dB, or up 3 dB.",
                                               "brEARk can't see your monitor knob (nor Live's master fader), so the estimate only follows if you tell it.");

inline const juce::String coach = tip ("The coach: what to do now, from brEARk's state.",
                                       "Your own lines can go in ~/Library/Application Support/brEARk/coach.txt.");

//── Buttons ───────────────────────────────────────────────────────────────────
inline const juce::String setup = tip ("Where you listen: MONITORS or HEADPHONES. Click to switch.",
                                       "Headphones put the sound right at the eardrum: brEARk counts them 3 dB louder.");

inline const juce::String preset = tip ("How loud your usual playback is: QUIET ~70 dB, NORMAL ~80 dB, LOUD ~90 dB. Click to switch.",
                                        "brEARk can't see your volume knob; this tells it where your normal level sits.");

inline const juce::String strictness = tip ("What happens when the battery is empty. Click to switch.",
                                            "GENTLE: one sound. ESCALATING: firmer reminders. HARD: the sound is cut at 0 %.");

inline const juce::String size = tip ("Window size: COMPACT or POSTER, saved with the Live set.",
                                      "POSTER adds six stats, the reasons for the drain, and HISTORY.");

inline const juce::String data = tip ("All raw measurements, the model's numbers and the developer controls.",
                                      "For checking what brEARk hears. NOT THAT LOUD and all settings are there too.");

inline const juce::String history = tip ("Today's ear battery as a curve, the last 7 days' WHO dose as bars.",
                                         "Shows when you rested, when you pushed on, and when brEARk was off.");

inline const juce::String breakNow = tip ("Starts a guided break with a countdown; the battery refills from the first second.",
                                          "It ends by itself when the time is up, or when you play for 10 seconds.");

inline const juce::String endBreak = tip ("Ends the guided break now.",
                                          "Playing for 10 seconds ends it too.");

inline const juce::String snooze = tip ("Plugs the sound back in for exactly 10 minutes.",
                                        "After that, the plug comes out again if the battery is still drained.");

inline const juce::String notThatLoud = tip ("Tells brEARk that what plays now (the last 2 minutes) is your normal listening level.",
                                             "It can't see your volume knob, so this corrects its level estimate from now on.");

//── HISTORY ───────────────────────────────────────────────────────────────────
inline const juce::String todayCurve = tip ("Your ear battery today, one point per minute from 05:00. Hatched: brEARk was switched off.",
                                            "The dashed line is 20 %, where the heads-up comes. Gaps: Live was closed.");

inline juce::String day (const juce::String& name, double dose, double listenedSeconds, int breaks, double offSeconds)
{
    auto shows = name + ": " + juce::String (juce::roundToInt (dose)) + " % of the WHO daily dose, "
               + juce::String ((int) listenedSeconds / 3600) + ":" + juce::String (((int) listenedSeconds / 60) % 60).paddedLeft ('0', 2)
               + " listened, " + juce::String (breaks) + (breaks == 1 ? " break" : " breaks");

    if (offSeconds >= 60.0)
        shows << ", " << juce::String ((int) offSeconds / 60) << " min off";

    return tip ((shows + ".").toRawUTF8(), "Red: over 100 %. The week allows 5 full days (500 % in total).");
}

inline const juce::String close = tip ("Back to the meter.", "HISTORY updates while it's open.");

inline const juce::String weeks = tip ("This week and last week, Monday 05:00 to Monday 05:00. The WHO weekly dose is 5 full days (40 hours at 80 dB(A)).",
                                       "Above 100 % the week costs more than ears can safely take. A quiet day brings it down.");

//── DATA ──────────────────────────────────────────────────────────────────────
inline const juce::String back = tip ("Back to the meter.", "Settings changed here apply at once.");
inline const juce::String timeWarp = tip ("Runs the model clock 60 times faster, to try alerts and HARD mode in minutes.",
                                          "Nothing is saved while it runs; TIME x1 brings back the real state.");
inline const juce::String testAlert = tip ("Shows the break pop-up and plays the alert sound.",
                                           "A test only: nothing changes in the model.");

inline juce::String dataRow (const juce::String& label)
{
    if (label == "STATUS")       return tip ("What brEARk detects right now, and the transport.", "OFF: switched off in Live. NO AUDIO: the host stopped. EXPORT: never counted.");
    if (label == "LOUDNESS")     return tip ("BS.1770 loudness: momentary (400 ms) and short-term (3 s) in LUFS. REF: your long-term normal.", "The gap between short-term and REF drives the level estimate.");
    if (label == "LEVEL (A)")    return tip ("A-weighted level of the last second, and the sample peak over 3 s.", "The A-weighted level drives the WHO dose.");
    if (label == "HARSHNESS")    return tip ("Presence (2-5 kHz) and highs (8-16 kHz) against the 250 Hz-2 kHz body, over 10 s.", "Above -10 / -12 dB the drain rises, up to x1.3.");
    if (label == "DYNAMICS")     return tip ("PSR: sample peak minus short-term loudness, over 10 s.", "Under 14 dB the drain rises, up to x1.2 at 7 dB.");
    if (label == "REPETITION")   return tip ("Playhead plays of the current bar, time on the same loop, and the loop strain.", "Strain: plays x log2 (1 + loop seconds), in minutes of a 4-bar loop. x1.2 at 45, pop-up at 60.");
    if (label == "LOOP CHECK")   return tip ("The loop detector's last comparison: rhythm match (1 = identical) and level shift.", "A loop needs rhythm 0.85 or more and a shift of 1.5 dB or less.");
    if (label == "NIGHT")        return tip ("Local time and the night factor.", "From 22:00 the drain rises, full (x1.25) from 01:00 to 05:00.");
    if (label == "DRAIN")        return tip ("All factors times the daily load, capped at x1.6, and the level creep over an hour.", "The drain multiplies how fast the battery empties.");
    if (label == "EAR BATTERY")  return tip ("The ear battery in %.", "0 %: break due. It refills in silence after a 60 s onset (63 % of the gap in 5 min).");
    if (label == "DOSE")         return tip ("Today's and this week's WHO dose in %.", "100 % today = 8 hours at 80 dB(A); the week allows 5 such days.");
    if (label == "EST. LEVEL")   return tip ("Estimated level at your listening position: perceived (drives the battery), A-weighted (drives the dose), and over 2 minutes.", "An estimate from your preset. NOT THAT LOUD corrects it.");
    if (label == "FORECAST")     return tip ("At the level of the last 2 minutes: time to an empty battery, the same at 6 dB less, time to a full daily dose, and your VOLUME setting.", "Forecasts assume the level stays. VOLUME follows the -6 / -3 / +3 buttons.");
    if (label == "TODAY")        return tip ("Listening time, time since the last break, breaks today, the suggested break length.", "A silence of 5 minutes or more counts as a break.");
    if (label == "BREAKS")       return tip ("The break controller's state.", "LISTENING, WARNED (20 %), DUE (0 %), ON A BREAK, or UNPLUGGED.");
    if (label == "POWER")        return tip ("HARD mode's power-down stage, and today's power-downs and snoozes.", "RUN: the audio passes untouched.");
    if (label == "ENGINE")       return tip ("Sample rate, plug-in format, precision and FFT size.", "Analysis runs on a background thread, never on the audio thread.");
    if (label == "AUDIO THREAD") return tip ("Time brEARk spends per audio block on Live's audio thread: average and maximum.", "It only copies the audio for analysis: a few microseconds.");
    if (label == "WORKER")       return tip ("The background analysis thread: share of one core, dropped samples, wake interval.", "Drops only happen when the CPU is saturated; the audio is never affected.");
    if (label == "EXPORTS")      return tip ("Exports seen, whether Live flagged them, and the render speed.", "Exports are never measured, alerted or muted.");
    return {};
}
} // namespace breark::ui::tips
