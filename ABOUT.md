# brEARk.

### A break coach for your ears. For producers who forget to stop.

You sit down to fix one snare. You look up and it's 2 a.m., the mix is harsh, the master is crushed, and your ears are ringing. brEARk sits on your master channel, listens to everything you hear, and tells you when to take a break, before your judgment goes and before your hearing pays for it.

![brEARk two hours into a session: OK., 45 % ear battery, break in about 17 minutes](docs/images/showcase-poster.png)

<sub>From a test run: brEARk listening to a mastered house loop at about −7 LUFS, after 2 h 14 min of listening today. The day so far was loaded for the test; the level, the drain and the forecasts were measured live by the plugin, and the image is drawn by its own UI code. The battery is draining faster than usual because the master is heavily limited (LITTLE DYNAMICS +20 %) and the day is already well used (FULL DAY +19 %).</sub>

---

## Why your ears need a coach

**Ears tire without telling you.** After an hour or two they adapt: the mix seems quieter, so you turn it up; the top end seems dull, so you add more; the limiter seems fine, so you push it harder. The next morning you hear what you did.

**Hearing damage is permanent.** The WHO puts the safe limit at about 80 dB(A) for 40 hours a week, and every 3 dB louder halves the time. A loud session can use up a whole week's share in an afternoon, and nothing in a DAW keeps count.

**Good decisions need fresh ears.** Balance, EQ, loudness and the final master are best decided early, after a break, not at hour four.

brEARk keeps count for you and says it out loud.

---

## Built for hyperfocus

If you lose track of time when the music is working, and many producers with ADHD know that feeling well, a timer you set yourself is easy to wave away. Hyperfocus doesn't check clocks.

brEARk is an outside clock that doesn't get pulled into the flow with you:

- **It watches so you don't have to.** No timers to set and nothing to remember: it starts listening the moment Live plays.
- **Its pop-ups find you.** They float over full-screen Live and on every Space, never steal your keyboard, and close on their own after a few seconds.
- **It's honest without nagging.** One big word tells you where you stand: **FRESH.** **OK.** **TIRING.** **BREAK.** **STOP.** In ESCALATING mode, reminders only get firmer if you keep going.
- **It can hold the line for you.** In HARD mode, brEARk pulls the plug when your ears are spent: the music slows down like a turntable losing power and stops, for at least 5 minutes. It never cuts a recording, and SNOOZE always buys you 10 minutes.
- **It tells you what's left.** "BREAK IN ~17 MIN" and "AT QUIET ~40 MIN" turn an abstract warning into a choice you can act on.

brEARk isn't a treatment for anything. It's the friend who taps you on the shoulder.

---

## The mistakes that creep in over time

brEARk watches for the conditions behind the classic late-session mistakes, and says so while you can still do something about it:

| What happens to tired ears | What brEARk does |
|---|---|
| The mix creeps louder because your ears adapted | Asks **HOW LOUD?** when the music drifts 6 LU or more from where you set your level |
| A bright, edgy mix wears you out (and tired ears ask for even more top end) | **BRIGHT.** when the highs make your ears drain 20 % faster |
| The master gets squashed because loud still sounds "fine" | **SQUASHED.** when peak-to-loudness (PSR) drops to about 8 dB |
| Loop blindness: an hour on the same 4 bars | **LOOP.**, only after a long time: looping is the job, but context matters |
| Big decisions at 2 a.m. | **LATE.** between 23:00 and 05:00, and a faster drain at night |
| Pushing through an empty battery | **BREAK.**, then firmer, or HARD mode's power-down |
| Overdoing the day or the week | **HEARING DOSE** with time left, **DONE.** when it's used up, and a weekly look back |

Read the big word as a schedule: **FRESH.** is the time for the big calls (balance, tone, the master); **TIRING.** is better spent arranging and editing.

---

## How it works

brEARk keeps two meters:

- **Ear battery:** how tired your ears are right now. It drains while you listen (faster when it's loud, bright, squashed, repetitive or late) and refills in silence. A minute of silence starts recovery; 5 minutes counts as a break.
- **Hearing dose:** how much of today's safe sound you've used, after the WHO. It doesn't refill during the day. At 100 % brEARk says **DONE.**

**Your ears set the level; brEARk measures the music.** brEARk can't hear your room or see your volume knob, so it never guesses dB at your ears. Instead you rate it: **QUIET** (you could talk normally), **NORMAL** (you'd raise your voice) or **LOUD** (you'd have to shout), and **MONITORS** or **HEADPHONES**. brEARk asks at the start of each session. From then on it measures how much louder or quieter the music is than when you rated it, and asks again when it drifts.

**You choose how strict it is:**

| | When the battery is empty |
|---|---|
| **GENTLE** | One sound and a pop-up |
| **ESCALATING** | Reminders get firmer the longer you keep going |
| **HARD** | The plug comes out: a vinyl spin-down, then silence for at least 5 minutes |

![HISTORY: today's ear battery, the last 7 days of hearing dose, this week and last week](docs/images/showcase-history.png)

<sub>HISTORY from the same test run: today's ear battery with one break, the last 7 days of hearing dose (Sunday used it up), and this week compared with last week.</sub>

Every pop-up and its trigger is listed in [docs/ALERTS.md](docs/ALERTS.md).

---

## Studio-safe by design

brEARk lives on your master, so it's built to be invisible there:

- **It never touches your audio.** The output is bit-identical to the input, proven by a bit-exact null test, with zero latency and no phase shift. The only exception is HARD mode's power-down, and only when you've asked for it.
- **Exports are sacred.** Bounces and renders are never measured, never alerted, never muted.
- **Practically no CPU.** On the audio thread brEARk only copies samples: about 0.02 % of one core. All analysis runs on a low-priority background thread.
- **Small and steady.** About 2–3 MB of memory, flat over hours. It's tested with three-hour soak runs and checked for any allocation or lock on the audio thread.
- **Private.** No account, no internet. Your listening history stays in a small file on your Mac.

---

## Honest numbers

brEARk is a coach, not a medical device. The hearing dose follows the WHO's safe-listening guidance; the ear battery is brEARk's own model of listening fatigue, an estimate tuned by ear. Because brEARk can't hear your room, the level on your ears comes from your own rating, and everything brEARk shows about the signal (LUFS, LU, PSR) is measured, not guessed. If your ears ring or feel muffled, stop for the day, and see a hearing professional if it doesn't pass.

---

## Get started

1. Put **brEARk** on Live's **master** track (one per set).
2. Press play. After a few seconds, **NEW SESSION.** asks where you listen and how loud it is. One click each.
3. Pick **GENTLE**, **ESCALATING** or **HARD**.
4. Make music. When brEARk says **BREAK.**, take it: as long as it suggests (5 to 25 minutes), in quiet, away from the speakers.

**Requirements:** macOS 11 or later, Apple Silicon or Intel. VST3 and Audio Unit. Made and tested in Ableton Live 11.

---

<sub>brEARk is made by Totemphonia Studio Berlin. Type: Space Grotesk and Space Mono (SIL Open Font License). Build instructions: [README.md](README.md). Concept: [CONCEPT.md](CONCEPT.md).</sub>
