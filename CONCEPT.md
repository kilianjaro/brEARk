# brEARk — Concept Spec v0.1

*Step 1 of 3 (Concept) · 2026-10-10 · Status: agreed · updated by step 2 → see [IMPLEMENTATION.md](IMPLEMENTATION.md)*

> A plugin on Ableton's master channel that listens to everything you hear and tells you **when to stop listening**, to protect your hearing and keep your ears fresh for good mix decisions.

---

## 1. Goals & audience

| | |
|---|---|
| **Audience** | Me first (macOS, Ableton Live). Code kept clean enough to release later (free or paid). |
| **Core goal** | Both, combined: **long-term hearing health** (noise dose) **and** **short-term listening fatigue** (fresh ears → better decisions). |
| **Not** | A medical device. Not a precise SPL meter. Not a mixing tool. It never colors the audio in normal operation. |

---

## 2. Core model: two layers

| Layer | Name in UI | What it models | Drains with | Recovers |
|---|---|---|---|---|
| Short-term | **EAR BATTERY** 100 → 0 % (main index) | Listening fatigue / loss of objectivity | Time × loudness × harshness × compression × repetition × time of day | During breaks, on a **realistic curve** (fast at first, then slower; ~15 min ≈ nearly full) |
| Long-term | **HEARING DOSE** 0 → 100 % used *(M11 name; was DAILY DOSE)*, e.g. "18 % USED · ~4:20 H LEFT" | Cumulative sound exposure (energy dose, WHO) | Time × your level setting × loudness against your anchor (equal-energy principle) | **Never during the day**; resets overnight |

Weekly stats complement the daily dose (reference frameworks to evaluate in step 2: WHO/ITU safe-listening weekly allowance, NIOSH 85 dB(A)/8 h with a 3 dB exchange rate).

---

## 3. What brEARk listens to

**Always:** listening duration and loudness (perceived loudness, LUFS-style, K-weighted).

**Extra factors (all four selected):**

| Factor | Affects | Idea |
|---|---|---|
| Spectral harshness | Battery (+ dose weighting) | Energy around 2–5 kHz and sharp highs tire faster at equal loudness |
| Loop repetition | Battery only | Detects hearing the same section over and over ("loop blindness"). *M9 (Kilian's rule): it's the number of plays that wears, each weighing log2 (1 + loop seconds): 6 plays of a 1 s loop weigh less than 6 of a 10 s loop, and a 60 s loop strains less per minute than a 10 s loop. Much gentler: at most +20 % drain. Looping is normal production work: the LOOP pop-up only comes after the equivalent of 1 hour of a 4-bar loop (1 bar ≈ 30 min, 16 bars ≈ 2.5 h)* |
| Over-compression | Battery | Low crest factor / dense, brickwalled audio is more fatiguing |
| Time of day / session load | Thresholds | Late night and back-to-back sessions lower the limits |

**Limitation, accepted:** the plugin only sees digital level (dBFS/LUFS) and cannot see the monitor or headphone volume knob.

---

## 4. Listening setup & level presets

- **Switch:** `MONITORS / HEADPHONES`, each with its own curve (headphones are stricter).
- **Level preset (no SPL calibration for now):** `QUIET / NORMAL / LOUD` maps digital loudness to an *estimated* SPL band.
- Because these are estimates, the UI shows **bands and percentages, never fake-precise dB SPL**.
- Optional real calibration (phone SPL app + reference noise) can be added later without changing the concept.

**M11 (Kilian): your ears set the level, brEARk measures the signal.**
- **QUIET / NORMAL / LOUD is the main control**, a three-way switch on the main window: how loud it is *on your ears* (QUIET: you can talk normally; NORMAL: you'd raise your voice; LOUD: you'd have to shout). Switch it when you turn the monitor knob.
- **A click while music plays anchors it:** "this loudness is NORMAL on my ears". From then on the signal counts relative to that moment, in LU: "NORMAL +4 LU" means 4 LU louder than when you set it. Before the first anchor, brEARk learns your usual loudness from playback.
- **Check-in:** a pop-up asks "How loud is it on your ears now?" with QUIET / NORMAL / LOUD, at the first sound of each session, and when the music stays 6 LU or more away from the anchor over 2 minutes of play (at most every 20 min). One click answers, anchors and closes it.
- **No dB at your ears anywhere** (no estimated level, no VOLUME buttons, no NOT THAT LOUD, no 88 dB LOUD pop-up). Shown are signal facts: loudness in LUFS, the LU against your anchor, peak dBFS in DATA.

---

## 5. Breaks & recovery

- **Detection, automatic + manual:**
  - Silence on the master or stopped playback counts as recovery automatically.
  - So does time when nothing reaches the ears: Live closed, the Mac asleep, or no audio from Live at all (audio device stopped, changed or unplugged).
  - A big **BREAK** button starts a guided break with a countdown. *M9: playing for 10 s ends it by itself, quietly, so the drain carries on without END BREAK.*
- **brEARk switched off in Live** (device off): everything **freezes**. Nothing drains, recovers or escalates, and the time shows as **OFF** in the history. *(M8 decision. If Live stops calling a switched-off plugin instead of bypassing it, brEARk can't tell that from a stopped audio device, and it counts as silence.)*
- **Recovery:** realistic curve. Short breaks count partially.
- **Break length:** **adaptive**. It scales with how drained the battery is and how heavy the day has been, from ~5 min (light) to 15–20+ min (heavy day). The coach announces it.

---

## 6. Strictness modes & escalation

Switchable in the UI: `GENTLE / ESCALATING / HARD`

|  | 20 % | 0 % | +10 min | +15 min | +20 min … |
|---|---|---|---|---|---|
| **GENTLE** | — | ♪ + banner | — | — | — |
| **ESCALATING** | banner (silent) | ♪ + banner, calm tone | ♪ + banner, firmer | ♪ + banner, BRUTAL | every 5 min, BRUTAL |
| **HARD** | banner (silent) | ♪ + **POWER-DOWN** at once *(M9: there was a 10 min grace; Kilian: "it came too late")* | muted | muted | muted until min. break done |

♪ = the user's mp3 played through the master.

**End of day** *(M10)*, at 100 % of the WHO daily dose while you keep listening: the state word becomes **DONE.**
- GENTLE: one pop-up with sound.
- ESCALATING: the same, then a reminder every 30 min of listening.
- HARD: the plug comes out and stays out until the new day starts at 05:00. SNOOZE buys 10 min, again and again (logged). Never during a take; leaving HARD plugs back in.

**Ending an alert:**
- In GENTLE and ESCALATING, any alert step ends as soon as you press BREAK or go silent.
- In HARD mode, the mute follows the rules in §7.

---

## 7. HARD mode: "pulling the plug"

**Why a full stop:** dulling the highs or lowering the volume would only make the producer compensate (more EQ, more volume knob). That is the opposite of the goal.

- **Power-down:** the live audio slows down like a **turntable losing power**: pitch and speed drop gradually to a full stop, then silence. This is the signal itself.
- **Mute lifts:** after a **minimum break** (e.g. 5 min, exact value in step 2), even if the battery isn't full.
- **Power-up:** when you return and audio plays again, it **spins back up** with a brief over-speed that catches up to live, then continues.
  - The spin-up is clearly **faster** than the spin-down (e.g. ~0.5–1 s vs. ~3–4 s).
- **Override:** **Snooze 10 min** buys exactly 10 more minutes, then the power-down returns. Every snooze is logged in the stats and the coach comments on it.
- **Recording guard:** the power-down never fires while Live is recording.
  - The coach warns: *"After this take: UNPLUG."*
  - The power-down happens as soon as recording stops.
- **Escape hatch:** deactivating the device in Live always works. That's fine; it's a self-commitment tool, not a prison.
- **Fallback:** if the vinyl effect proves too fiddly, use mp3 + a simple fade.
- **Must never** affect exports or offline renders.

---

## 8. Alerts

The plugin window is usually closed, so alerts must reach you anyway:

1. **The user's mp3 via macOS system output** (changed in step 2)
   - It plays completely outside Live's audio stream, so the plugin output stays bit-identical.
   - It can never end up in a bounce, resample or print.
   - Requirement: the Mac's default output should be the studio interface.
2. **brEARk's own toast window** (changed in step 2: replaces the native macOS banner)
   - A small brutalist window at the top-right corner.
   - Always on top, also over full-screen Live, and not blocked by Focus / Do Not Disturb.
   - Never steals keyboard focus.
   - Has BREAK / SNOOZE buttons.

---

## 9. The coach

| | |
|---|---|
| **Language** | English |
| **Timing** | Mostly quiet. Speaks at **events**: thresholds, during breaks, notable moments (level creep, loop count, late hour). Otherwise a calm status line. *M9: the LOUD pop-up needs about 88 dB held for 2 minutes (not a loud moment), says it's an estimate, and offers **NOT THAT LOUD**: one click makes what plays now your normal level (your preset's dB). Also in DATA.* |
| **Tone shifts with severity** | Dry & witty while fresh → calm mentor when tired → terse brutalist commands when ignored. The tone itself is a signal. |
| **Topics** | Break instructions · creative break tasks · ear-health facts · live listening hints |

Example lines:
- *Witty (fresh):* "It looks like you're mixing at 2 am. Would you like help going to bed?"
- *Mentor (tired):* "Take a break of more than 10 minutes and take care of your ears."
- *Creative task (break):* "Work on the track in your head for the next 5 minutes."
- *Mindful (break):* "Notice the sounds and the silence in your studio."
- *Hint (live):* "Your level crept up 4 dB in the last hour. Check the mix at whisper volume."
- *Brutal (ignored):* "STOP. 10 MIN. NO AUDIO."

---

## 10. UI & design

**Direction:** clean, minimal, brutalist. Function is reflected in form.

- **Hero:** a **one-word poster**, a huge bold state word.
  - Proposal: `FRESH.` → `OK.` → `TIRING.` → `BREAK.` → `STOP.`, plus `RESTING.` and `UNPLUGGED.`
  - Built (M7/M8): also `INACTIVE.` (a second instance) and `OFF.` (switched off in Live).
- **Texture:** **raw technical data**, small, monospaced, almost decorative, but true:
  - EAR BATTERY % and DAILY DOSE bar
  - Time stats: listening today, since last break, breaks taken
  - Loudness: short-term LUFS, estimated level band, level creep
  - Factor breakdown: harshness, dynamics, loop count, time of day, i.e. *why* it's draining. *M9: in plain words and percentages ("BATTERY DRAINS 21 % FASTER: HARSH HIGHS +3 % …")*
- **Hover explanations** *(M9)* on every button and value: one sentence on what it shows (technical), one on what it means.
- **Time left** *(M10, M11 wording)*: "BREAK IN ~42 MIN · AT QUIET ~1:37 H", forecast at your level and the loudness of the last 2 minutes of play; the hearing dose line says how long it lasts ("~4:20 H LEFT"). While resting: how long until 90 %.
- *(M10 had VOLUME -6 / -3 / +3 buttons; M11 replaced them with the anchored level switch.)*
- **Tidy, two sizes** *(M11)*: COMPACT shows the time line, the hearing dose, the signal and the level switch; POSTER adds the week's hearing dose, listening time and time since the break. The drain reasons appear only when one adds 10 % or more ("DRAINS FASTER: LITTLE DYNAMICS +16 %").
- **History graph** (today's curve + weekly bars) is **hidden behind a button**, not always visible. Time switched off is hatched and labelled **OFF**, with a daily total.
- **Controls:**
  - `MONITORS/HEADPHONES` · `QUIET/NORMAL/LOUD` · `GENTLE/ESCALATING/HARD`
  - **BREAK** button
  - **SNOOZE** (when unplugged)
- **Coach line:** a text area at the bottom of the window, `> …`
- **Type:** Space Grotesk (poster word) and Space Mono (data).
- **Window:** two presets, **COMPACT** and **POSTER**.
- **Refresh:** a calm 4 updates per second.
- **Settings:** global, shared by all Live sets.

---

## 11. Persistence & stats

- Exposure is remembered **daily, across Live sessions and projects**. Reopening Live does not reset it.
- **Weekly history** for long-term awareness.
- Logged: listening time, dose, breaks, snoozes, power-downs, time switched off, longest stretch without a break, times the battery ran empty.
- **Weekly summary** *(M10)*: once a week, the first time you listen after Monday 05:00, a pop-up with last week (share of the WHO weekly dose, days over the daily dose, time listened, longest stretch, empty battery, breaks). This week and last week also in HISTORY.

---

## 12. Ground rules

1. **Bit-transparent pass-through:** the audio is never altered (proven by a null test), with 0 latency and no phase shift. The **only** exception is the Hard-mode vinyl power-down/up.
2. **Exports don't count:** offline/non-realtime renders are neither measured nor alerted nor muted.
3. **Premium-studio safe:** near-zero audio-thread cost, analysis on a low-priority thread, small fixed memory footprint.
4. **Honest numbers:** estimates are labeled as estimates; the plugin is not a medical device.
5. **One brain:** designed for one instance on the master. Extra instances must not double-count.

---

## 13. Implementation

The implementation is specified in **[IMPLEMENTATION.md](IMPLEMENTATION.md)**: architecture, the science-vs-engineering model, DSP, quality gates and milestones M0–M8.
