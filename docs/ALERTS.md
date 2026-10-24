# brEARk: what it does and every alert

*As of version 1.0. Keep this file in step with the code when alerts change.*

---

## What brEARk does

brEARk sits on the master and listens. It never changes the audio, except for the power-down in HARD mode. It keeps two meters:

- **Ear battery** (100 → 0 %): how tired your ears are, which also means how reliable your judgment is right now. It drains while you listen and refills in silence. Recovery starts after 60 s of silence; 5 minutes or more counts as a break.
- **Hearing dose** (0 → 100 % used): how much of today's safe sound you've used, based on the WHO (8 h at 80 dB(A) a day, 40 h a week). At your NORMAL setting a day is **8 hours**, at LOUD 2.5 hours, at QUIET practically unlimited; music louder than when you set the level uses it faster (+3 LU halves the time). Headphones don't change the dose, only the battery. It doesn't refill during the day; a new day starts at 05:00.

Both depend on **your rating** (QUIET / NORMAL / LOUD and MONITORS / HEADPHONES) and on how loud the music is compared with the moment you set that rating. brEARk never shows dB at your ears: it can't see your monitor knob, so you rate the level, and it measures the signal.

Loudness itself doesn't trigger a warning pop-up. It drives the battery and the hearing dose, and it triggers the HOW LOUD? check-in when it changes a lot.

---

## What it measures, and what each measurement affects

| Measurement | Affects |
|---|---|
| Loudness (LUFS, ITU-R BS.1770) compared with your setting | How fast the battery drains and the dose fills; HOW LOUD? check-ins |
| A-weighted level | The hearing dose |
| Brightness (2–5 kHz and 8–16 kHz compared with 250 Hz–2 kHz) | Drain up to +30 %; BRIGHT. hint |
| Dynamics (PSR: peak minus loudness) | Drain up to +20 %; SQUASHED. hint |
| Repetition (loops on the tempo grid, playhead) | Drain up to +20 %; LOOP. hint |
| Time of day (22:00–05:00) | Drain up to +25 %; LATE. hint |
| How much of today's dose is used | Drain up to +50 % ("FULL DAY") |
| All the drain reasons together | Capped at +60 % |
| Silence, stopped transport, the Mac asleep, no audio from Live, HARD mode's mute | Rest: the battery refills |
| Recording | HARD mode never cuts a take |
| Exports | Not counted, never muted |
| brEARk switched off in Live | Everything freezes; HISTORY shows the time as OFF |

---

## Every pop-up

**About sound:** ♪ means the pop-up plays brEARk's alert sound through macOS (not through Live, so it never ends up in a bounce). **SOUND ON / OFF** in the window turns it off; the pop-ups keep coming, silently.

**How long pop-ups stay:**
- Message pop-ups close after **6 s**; HEADS UP. after **10 s**.
- Pop-ups with a button (BREAK NOW, SNOOZE) stay **30 s**.
- Questions (WELCOME., NEW SESSION., HOW LOUD?) **stay until you answer** or close them with X.
- A timed pop-up stays open while the mouse is over it, and closes 1.5 s after you move away.
- Every answer closes it. X is only for closing early.
- If another pop-up arrives while a question is open, the question comes back after it. Setting the level in the window or in DATA answers an open question.
- What you just clicked yourself (BREAK, SNOOZE) gets no pop-up: the window shows it.

### 1. Break alerts (ear battery)

| When | GENTLE | ESCALATING | HARD |
|---|---|---|---|
| Battery at 20 % | – | **HEADS UP.** | **HEADS UP.** |
| Battery empty, you're playing | **BREAK.** ♪ (once) | **BREAK.** ♪ | **UNPLUGGED.** ♪ in red, with the spin-down |
| +10 min of listening | – | **BREAK.** ♪ (firmer) | muted for at least 5 min |
| +20 min, then every 15 min | – | **STOP.** ♪ in red | – |

- The clock after "empty" only counts listening time; silence pauses it.
- Once the battery is back at 30 %, everything starts over.
- HEADS UP., BREAK. and STOP. have a **BREAK NOW** button.

**HARD only:**
- **AFTER THIS TAKE.** If you're recording when the plug would come out, it waits and pulls the plug right after the take.
- **SNOOZE** (in the pop-up or the window) gives you exactly 10 minutes, then the plug comes out again if you're still drained. The window shows SNOOZED with the time left.
- **BACK ON.** After the 5-minute minimum break, or when you leave HARD mode. The sound spins back up when music plays.

### 2. End of day (hearing dose at 100 %, while you play)

| GENTLE | ESCALATING | HARD |
|---|---|---|
| **DONE.** ♪ once | **DONE.** ♪, then again every 30 min of listening | **DONE.** ♪ in red: unplugged until 05:00; SNOOZE 10 min, as often as you need (each one logged) |

HARD never unplugs during a take here either (AFTER THIS TAKE.), and leaving HARD plugs back in.

### 3. Your own breaks

- Pressing **BREAK** starts a guided break: the window shows RESTING. with the time left, and the coach line changes every minute.
- **WELCOME BACK.** The break countdown is over.
- If you start playing during the break, it ends after 10 seconds of continuous sound, without a pop-up.

### 4. Check-ins

- **WELCOME.** A new user's very first question, at the first music: one sentence on what brEARk does, then MONITORS / HEADPHONES and QUIET / NORMAL / LOUD. Until you answer it, the window's coach line says what to do.
- **NEW SESSION.** The same question at the **first music of the day**, and after **2 hours without sound** (Live closed, the Mac asleep, a long pause). A quick restart of Live keeps your last answer and doesn't ask. MONITORS / HEADPHONES applies at once and keeps the pop-up open; QUIET / NORMAL / LOUD answers and closes it.
- **HOW LOUD?** When the music has been 6 LU or more louder or quieter than your setting over 2 minutes of play, at most every 20 minutes. QUIET / NORMAL / LOUD answers, anchors the level, and closes it.
- **The anchor listens for about 2 minutes:** the last 30 s of play before your click and the next 90 s after it, so a quiet intro or a soloed track can't set the level.

Setting the level yourself (the switch in the window or in DATA) counts as an answer.

### 5. Coach hints

All five are silent, with no buttons. At most one hint every 20 minutes, only after 2 minutes of sound in a session, only while sound plays, never during a break, and **each at most twice a day** (restarts included). If several apply, the one higher in this list wins:

| Hint | When | Not again for |
|---|---|---|
| **LOOP.** | Long on the same loop: loop strain 60, about 30 min of a 1-bar loop, 60 min of 4 bars, 2.5 h of 16 bars (each play counts log2 (1 + loop seconds)) | 60 min |
| **LATE.** | Between 23:00 and 05:00 | 6 h |
| **BRIGHT.** | The highs add 20 % or more to the drain (presence about -6 dB or air about -8 dB against the body) | 90 min |
| **SQUASHED.** | PSR around 8 dB or less | 3 h |
| **HALF DOSE.** | Hearing dose between 50 and 99 % | once a day |

After its second pop-up, a hint's topic still shows in the window's DRAINS FASTER line.

### 6. Weekly summary

**LAST WEEK.** appears once a week, the first time you play after Monday 05:00 (not again after restarting Live). It shows:
- your share of the weekly hearing dose;
- days that used up their dose;
- time listened and longest stretch;
- how often the battery ran empty, and your breaks.

There's also **TEST ALERT** in DATA, which shows the BREAK pop-up and always plays its sound.

---

## Feedback that isn't a pop-up

- **The state word:**
  - FRESH. (battery above 70 %), OK. (above 40 %), TIRING. (below);
  - BREAK. and STOP.;
  - RESTING. and UNPLUGGED.;
  - DONE.;
  - OFF. (switched off in Live) and INACTIVE. (a second brEARk in the same Live).
- **The colours:** the black area is the battery level. It turns red at 20 % or less. The whole window turns red for STOP., UNPLUGGED. and DONE. in HARD.
- **The coach line** at the bottom of the window. During any break (or a minute of silence) it changes every minute, in turn: a thought on the track, a care tip, a thought on the craft, an ear fact. Otherwise an occasional line. About 370 lines in all; no line comes back until the rest of its kind was shown, and brEARk remembers that across restarts (`coach-memory.json`).
- **The window's lines** (one line, one job: nothing is said twice):
  - under the big word, what's happening: e.g. HEARING DOSE USED UP · CALL IT A DAY, EAR BATTERY EMPTY, SOUND BACK IN 3:10;
  - above the numbers, only times: BREAK IN ~… · AT QUIET ~… while playing, 90 % IN ~… in silence, TAKE ~12 MIN OF QUIET when a break is due;
  - HEARING DOSE … % USED · ~… LEFT;
  - the signal: LUFS and LU compared with your setting, or SILENCE;
  - DRAINS FASTER: …, only when a reason adds 10 % or more.
- **Hover explanations** on every button and value: what it shows, and what it means for you.
- **HISTORY** (POSTER): today's battery curve, 7 days of hearing dose, this week and last week. **DATA** has every raw number, the version, and **HELP** (brEARk's guide on GitHub, in your browser).
- **The sound itself** in HARD: the vinyl spin-down, and the spin-up when you're back.
