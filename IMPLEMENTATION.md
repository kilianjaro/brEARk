# brEARk — Implementation Plan v0.1

*Step 2 of 3 (Implementation strategy) · 2026-10-10 · Builds on [CONCEPT.md](CONCEPT.md)*

---

## 0. Decisions at a glance

| Area | Decision |
|---|---|
| Toolchain | **CMake + Ninja**, JUCE **8.0.13** pinned in the project, Xcode 26.5 toolchain, arm64 (universal for release builds) |
| Formats | **VST3 + AU** for Ableton Live 11.3, plus a **Standalone** app for development |
| Identity | Company *Totemphonia Studio Berlin* · bundle `com.totemphonia.brEARk` · manufacturer code `Totm` · plugin code `Brek` · category *Fx / Analyzer* |
| Workflow | Claude writes code in milestones; Kilian builds, loads in Live, listens and tests |
| Version control | git with a private GitHub repo ([`kilianjaro/brEARk`](https://github.com/kilianjaro/brEARk)), one commit per milestone |
| Audio path | **Bit-transparent pass-through, 0 latency.** The only exception is the Hard-mode vinyl power-down/up |
| Alert sound | The user's mp3 is played via **macOS system output** (AVAudioPlayer), outside Live's audio stream |
| Banner | brEARk's **own toast window**: always on top, never steals focus, has BREAK / SNOOZE buttons |
| Analysis | **Low-priority worker thread**, about 20 frames/s; it thins itself out when the CPU is saturated |
| Dose standard | **WHO safe listening**: 80 dB(A) × 40 h/week, 3 dB exchange rate |
| Weighting | **A-weighting** for dose · **K-weighting / LUFS** (BS.1770) for display and fatigue |
| Fatigue model | **Energy bucket + tuned factors**, all constants in one documented file |
| Repetition | **Hybrid**: playhead bar-histogram + audio fingerprint, take the higher value |
| Hard mode + recording | Never cut while Live is recording; wait until recording stops |
| UI | **Design B, the window is the meter**: a black mass fills the window to the battery level, the type inverts where it crosses (red mass at ≤ 20 %, whole window red for STOP. / UNPLUGGED.) · **Space Grotesk Bold + Space Mono** (OFL, embedded) · two presets: **COMPACT 520×320 / POSTER 960×600** (POSTER adds the stats row and HISTORY) · calm **4 Hz** refresh |
| Coach texts | Built in, plus an optional user file `~/Library/Application Support/brEARk/coach.txt` |
| Settings scope | **Global** (setup, level preset, strictness), shared by all Live sets |

---

## 1. Non-negotiable audio rules

These rules apply to every line of code. Each one is verified by a test (§8).

1. **Never write to the audio buffer** in normal operation. Host output is then bit-identical to host input.
2. **Native 64-bit path:** `supportsDoublePrecisionProcessing() = true`. A 64-bit host is never forced through float.
3. **Latency = 0, tail = 0.** No filters, look-ahead or oversampling in the signal path, so there can be no phase shift and Live's plugin delay compensation never changes.
4. **The audio thread does almost nothing.** Per block it does exactly this:
   - copy samples into a pre-allocated lock-free FIFO;
   - store a few atomics (playhead, recording, realtime flag).
   
   It never allocates, locks, logs, touches files or the UI, and never waits.
5. **Exports are sacred.** During offline/non-realtime renders brEARk does not measure, does not alert, and the power-down is forced to pass-through. Even an UNPLUGGED brEARk exports a clean file.
6. **Graceful degradation.** If the worker falls behind, samples are dropped, never the audio thread's deadline. The model fills the gap with the last known level.
7. **Memory is pre-allocated.** Everything is allocated in `prepareToPlay` or on the message thread. Target total footprint is **< 6 MB**.

---

## 2. Architecture

```
AUDIO THREAD (realtime, owned by Live)   WORKER THREAD (low prio, 1 per Live process)   MESSAGE THREAD (UI)
──────────────────────────────────────   ─────────────────────────────────────────────  ──────────────────────
processBlock(buffer)                     every ~50 ms:                                   Editor Timer @ 4 Hz
 ├─ untouched → output                    drain SampleFifo                                 read Snapshot → repaint
 ├─ [Hard mode only] PowerDown            ├─ K-filter → LUFS-M / LUFS-S (BS.1770)        Event queue (async):
 ├─ copy → SampleFifo (lock-free SPSC)    ├─ FFT (Accelerate/vDSP) per ~43 ms frame        ├─ ToastWindow
 └─ playhead/recording/offline → atomics  │    → A-weighted level, harshness, bands        ├─ AlertSound (AVAudioPlayer)
                                          ├─ peak/RMS → PLR (dynamics)                     └─ Coach line
                                          ├─ fingerprint + playhead → repetition
                                         every 1 s:  ExposureModel.tick() → BreakController
                                         every 60 s: Persistence.save() (atomic temp+rename)
                                         publish Snapshot (lock-free) ─────────────────────►
                     ◄──── atomic powerState target (RUN / SPIN_DOWN / HOLD / SPIN_UP) ────
```

### One brain per Live process

- A `juce::SharedResourcePointer<Brain>` owns the worker, the model, persistence and settings.
- If multiple brEARk instances exist, **the first one is active** and feeds the FIFO. The others show `INACTIVE — ONE BRAIN IS ALREADY LISTENING.` and do nothing.
- If the active instance is removed, the next one takes over. Nothing is ever double-counted.

### Thread communication

| From → To | Mechanism |
|---|---|
| Audio → Worker | `SampleFifo`: `juce::AbstractFifo` over a pre-allocated buffer, 96 000 frames (2 s @ 48 kHz, 750 KB). Sized for macOS coalescing background-thread wakes to ~150 ms (measured) |
| Audio → Worker | Atomics: isPlaying, isRecording, ppq / bar, isNonRealtime, dropped-sample counter |
| Worker → UI | `Snapshot` struct copied under a mutex. Both sides are non-realtime; the audio thread never touches it |
| Worker → Message thread | Events (WARN, DUE, UNPLUG, BREAK_DONE, LEVEL_CREEP, …) via a lock-free queue, drained by a message-thread timer |
| UI → Worker | Atomics: BREAK pressed, SNOOZE, strictness, setup, preset |
| Worker → Audio | Atomic `powerState` target. The audio thread performs the ramps itself, sample-accurately |

---

## 3. Analysis pipeline (worker thread)

| Measure | Method | Rate | Notes |
|---|---|---|---|
| **LUFS-M / LUFS-S** | ITU-R BS.1770: K-weighting biquads, per-channel power sum, 400 ms / 3 s windows | 10 Hz | Time-domain filter, so the display matches Live / Youlean meters. Costs ~0.01 % CPU |
| **Spectrum** | One real FFT per ~43 ms frame (2048 @ 44.1/48 kHz, scaled up at higher rates), Hann window, JUCE `dsp::FFT` → Apple vDSP | ~23 Hz | Feeds the four rows below |
| **dB(A) (dose)** | FFT power × A-weighting table (IEC 61672), window-power corrected | ~23 Hz | A table lookup, so it's free |
| **Harshness** | Energy at 2–5 kHz and 8–16 kHz **relative to the 250 Hz–2 kHz body** (not the total, so bass-heavy mixes aren't automatically "smooth") → index 0–1 | ~23 Hz, smoothed 10 s | The ear's sensitivity peak (ISO 226) |
| **Dynamics** | PSR = sample peak over 3 s − LUFS-S (matches Youlean's PSR within ~0.5 dB) | 10 Hz, smoothed 10 s | ≥ 14 dB = dynamic, ≤ 7 dB = brickwalled |
| **Repetition, audio** | *M3.2: tempo-grid loop detection.* The audio thread stamps each block with the host's beat position (ppq, tempo); the worker takes **one FFT exactly on every 16th note**, so every pass of a loop is analysed at the same musical moments, with no misalignment and no smoothing. It compares the last ≥ 4 bars with the bars 1–16 bars earlier in played order, which covers Session clips and Arrangement loop braces alike. A loop needs (1) **rhythm**: correlation of 16th-to-16th changes per band ≥ 0.85, so LFO drift and fader/EQ moves are ignored, and (2) **level shift** ≤ 1.5 dB RMS with the fader removed, so 'same drums, new chords' is not a loop. Bands without rhythm (pads, drones) and bands 50 dB below the loudest are skipped. Grace = one loop length + 4 s | 1 FFT per 16th + check once per second | ~180 KB. Tests (realistic house): identical 0.99, humanised 0.95, heavy humanised 0.86–0.94 → correct 2-bar loop; arrangement brace → 8 bars; new chords never; EQ tweak/fader keep counting; 0.008 % CPU. *Root cause of the M3 miss: smoothing over ~1 beat plus frame misalignment; the old detector only matched lucky multiples of the loop length* |
| **Repetition, playhead** | Bar index from `ppqPosition` → decaying play-count per bar | per block, via atomics | Exact in Arrangement; catches loop-brace cycling |
| **Silence** | LUFS-S < −55 or transport stopped with no signal | 10 Hz | Recovery starts after **60 s** of continuous silence (micro-pauses while editing don't count) |

**Sample rates** from 44.1 to 192 kHz are supported:
- The FFT size scales so frames stay ~43 ms long.
- At ≥ 88.2 kHz only every 2nd frame is analysed. This is statistically equivalent over minutes.

---

## 4. The model: what's science and what's engineering

| Part | Basis | Confidence |
|---|---|---|
| Loudness display (LUFS) | ITU-R BS.1770 / EBU R128, exact | High |
| A-weighting | IEC 61672, exact | High |
| Daily / weekly dose method | WHO/ITU-T H.870 equal-energy principle, 3 dB exchange rate | High (method) |
| **Absolute SPL estimate** | Level presets instead of calibration → **about ±6 dB uncertainty (= dose ×4 / ÷4)** | **Low**, so the UI shows bands and %, never exact dB SPL. Optional calibration later. |
| EAR BATTERY | Engineered heuristic anchored to studio practice ("~10 min break per hour at ~80 dB") | Medium, tunable |
| Factors (harshness, dynamics, repetition, time of day) | Literature-inspired heuristics: ear sensitivity, density, habituation, circadian fatigue | Labelled as heuristics in code and UI |

### 4.1 Estimated level (hybrid model, revised in M2)

brEARk can't see the monitor knob, so a preset states how loud your *typical* playback sounds. Deviations from your own long-term loudness (the reference) move the estimate:

```
L_perceived = preset + headphones + (LUFS_S − reference)     → drives the EAR BATTERY
L_A         = preset + headphones + (dBFS(A) − reference)    → drives the DAILY DOSE
reference  ← slow average of your playback LUFS (time constant ~2 h, persisted)
```

| Preset | QUIET | NORMAL | LOUD | HEADPHONES |
|---|---|---|---|---|
| Typical playback | ~70 dB (talk easily) | ~80 dB (raise your voice) | ~90 dB (shout) | +3 dB |

**Why two weightings (M1 finding):** Kilian's master measured −8.7 LUFS but −22 dBFS(A).
- Bass-heavy music at the same perceived loudness carries ~13 dB less A-weighted dose, which is what the standards intend.
- Fatigue follows perceived loudness, so the battery uses LUFS.

**Why hybrid (Kilian's answer):** sometimes the monitor knob gets turned down for loud masters, sometimes not.
- A short louder session counts as louder.
- A whole day at master loudness slowly becomes the new normal.

### 4.2 Daily and weekly dose (WHO)

```
dose_day[%]  += 100 · 10^((L_A − 80) / 10) · dt / 8 h      → 100 % = 8 h @ 80 dB(A) = 1 h @ 89 = 15 min @ 95
dose_week[%]  = Σ dose_day(last 7 days) / 5                 → 100 % = WHO 40 h @ 80 dB(A)
```

- Silence (or a muted UNPLUGGED state) adds 0.
- The day rolls over at **05:00** local time.

### 4.3 EAR BATTERY

```
drain[%/min] = ( 100/240  +  (100/70 − 100/240) · 2^((L_perceived − 80)/5) ) · F      ← 5 dB rule (M3.1; was 3 dB)
F = F_harsh(1.0–1.3) · F_dyn(1.0–1.2) · F_rep(1.0–1.5) · F_night(1.0–1.25) · F_day(1 + 0.5·min(dose_day,1)) ;  F capped at 1.6 (M3.1; was 2.0)
```

The first term is a time-only drain: even at whisper level, the battery empties within 4 h.

With neutral factors, the battery empties after:

| L_perceived | 74 dB | 77 dB | **80 dB** | 83 dB | 86 dB | 89 dB |
|---|---|---|---|---|---|---|
| Battery empty after | 117 min | 92 min | **70 min** | 51 min | 37 min | 25 min |

**M3.1 tuning (Kilian's Live test: battery empty after ~6 min of headphone work on a −7.5 LUFS master):**
1. A fresh reference now learns like a running average: within ~0.2 dB after 10 min instead of 2 h.
2. The battery follows a 5 dB rule. The 3 dB rule stays for the dose, where it belongs.
3. The harshness neutral point is moved to typical modern masters.
4. The combined factor cap drops from ×2.0 to ×1.6.

The same session now lasts ~40 min on headphones and ~57 min on monitors.

**Recovery:** after 60 s of continuous silence (or on BREAK / UNPLUGGED), the battery moves towards 100 % on an exponential curve:

`b(t) = 100 − (100 − b₀) · e^(−t/τ)`, with τ = 5 min.
- 5 min recovers 63 % of the deficit.
- 15 min recovers 95 %.

The dose never recovers. While Live is closed, recovery is calculated from timestamps at the next load.

**Adaptive break length:** `t_break = τ · ln((100 − b)/10) · (1 + 0.5·min(dose_day,1))`, clamped to 5–25 min. Examples:
- empty battery on a fresh day → ~12 min;
- empty battery on a heavy day → ~17 min.

### 4.4 Break controller (state machine)

```
LISTENING ─20%→ WARNED ─0%→ DUE ─+10m→ OVERDUE ─(every 5 min)→ …
    ▲                          │ HARD: +10 min grace → [recording? wait] → UNPLUGGING (3.5 s) → UNPLUGGED
    │                          │                                                      │ min break 5 min
    └──── RESTING ◄────────────┴──── silence ≥ 60 s / BREAK ◄─────────── SPINNING_UP (0.8 s) ◄──┘
                                       SNOOZE (Hard only): +10 min, logged
```

Escalation timing per strictness mode follows CONCEPT.md §6.

### 4.5 All tunables in one place

`source/engine/ModelConstants.h` holds every number from this section. Each one is documented with its source or rationale, so tuning never means hunting through code.

---

## 5. Hard mode: the vinyl power-down DSP

- **Spin-down (~3.5 s):** the audio is read from a ring buffer with a playback rate going 1 → 0, using 4-point Hermite interpolation.
  - The amplitude follows the rate. A moving-magnet cartridge's output is proportional to stylus velocity, so the sound gets lower *and* quieter, like a real deck.
  - It ends in exact digital silence, with a short final fade so there's no click or DC offset.
- **Spin-up (~0.8 s):** the rate goes 0 → ~1.6 → 1, shaped so that ∫rate·dt = T_up.
  - That means the read head lands **exactly on the live write head**: the over-speed is the catch-up.
  - Hand-off to the untouched live signal uses a 10 ms crossfade as a safety net.
- **Cost:**
  - CPU is used only during the two ramps.
  - The ~2 s stereo buffer is allocated on the message thread when Hard mode is switched on, and freed when it's switched off.
- **Exports:** the effect is always bypassed.
- **Fallback:** a plain gain fade, if the vinyl effect proves troublesome.
- **As built (M5):**
  - The speed curves are a raised cosine down, and `t + (π/4)·sin(πt)` up; that curve's integral is exactly 1, so the read head catches up.
  - The read head trails the live signal by 3 samples, giving room for interpolation; a 5 ms crossfade in from dry and a 10 ms crossfade out to dry hide the switch.
  - The ring is 2 s, a power of two (1 MB at 48 kHz). The message thread creates and deletes it; the audio thread adopts and retires it through atomics and never waits.
  - While unplugged, the model counts the time as silence.
  - Exports pass untouched via the host flag or the speed heuristic.

---

## 6. Platform pieces (Objective-C++, macOS)

| Piece | Implementation |
|---|---|
| Alert sound | `AVAudioPlayer` on the embedded mp3 (`BinaryData` → `NSData`), triggered from the message thread. Plays on the macOS default output, completely separate from Live's stream. The mp3 is decoded by macOS, so no decoder is in our code. |
| Toast window | A JUCE `Component` on the desktop, borderless. Native tweaks via `NSWindow`: floating level, `hidesOnDeactivate = NO`, `canJoinAllSpaces | fullScreenAuxiliary`, non-activating (never steals keyboard focus). Created on demand, auto-dismissed. |
| Persistence | `~/Library/Application Support/brEARk/state.json`, written atomically (temp + rename) about once a minute, under 50 KB. |
| Export detection | `isNonRealtime()` (VST3 `kOffline` / AU offline render), **or** audio arriving faster than the wall clock. **Verified M1: Live 11 sets the offline flag for VST3 exports.** AU relies on the speed check |

---

## 7. Project layout

```
brEARk/
├─ CMakeLists.txt            JUCE 8.0.13 via FetchContent (pinned tag)
├─ CONCEPT.md · IMPLEMENTATION.md
├─ assets/
│  ├─ alert.mp3              ← your notification sound
│  ├─ fonts/                 SpaceGrotesk-Bold.ttf (static 700 instance) · SpaceMono-Regular/Bold.ttf · OFL licences
│  └─ coach/lines.txt        built-in coach lines (tagged by tone/topic/trigger)
├─ source/
│  ├─ PluginProcessor.*      audio thread: pass-through, FIFO push, PowerDown, atomics
│  ├─ PluginEditor.*         COMPACT / POSTER window, buttons, 4 Hz timer
│  ├─ DebugView.*            DATA: raw numbers, switches, TEST ALERT, build ID
│  ├─ engine/                Brain · SampleFifo · AnalysisWorker · Loudness · Spectrum · Fingerprint
│  │                         ExposureModel · BreakController · ModelConstants · Persistence · Snapshot
│  ├─ dsp/PowerDown.*        vinyl spin-down / spin-up
│  ├─ coach/Coach.*          event → line, tone by severity, no-repeat shuffle, user file
│  ├─ platform/              AlertSound_mac.mm · ToastWindow.* (+ _mac.mm)
│  └─ ui/                    Look (palette, fonts, buttons) · MeterView · HistoryView · ToastWindow · SegmentedControl
├─ tests/                    unit tests (model, loudness, fifo, power-down, coach, meter) · NullTest
└─ tools/RenderUI.cpp       renders every UI state to PNG (design review without Live)
```

---

## 8. Quality gates

| Gate | Tool | Pass criterion |
|---|---|---|
| **Null test (automated)** | `brEARk_NullTest`: hosts the installed VST3 + AU, noise incl. denormals / −0 / >0 dBFS, irregular blocks, mono/stereo, 44.1–192 kHz, 32/64-bit | 24/24 bit-identical. Mutation-checked: catches a 1-ULP gain change and a 64→32-bit truncation |
| **Null test (in Live)** | Live: duplicate track, brEARk on one copy, invert polarity on the other | Sum is **−∞ dB**, bit-exact |
| Export integrity | Export with brEARk UNPLUGGED vs. without brEARk | Files null to −∞ |
| Plugin robustness | **pluginval** strictness 10 (VST3 + AU), **auval** (AU) | No failures |
| Realtime safety | Code rule + review; Clang RealtimeSanitizer if available in Xcode 26 | No alloc/lock on the audio thread |
| CPU | `PerformanceTests` benchmark, debug view, Live CPU meter, Instruments | Audio thread unmeasurable; analysis < 0.1 % of one core. *Measured M1: K-filter 0.041 % + FFT 0.043 % = 0.085 % on a P-core; 0.15–0.2 % of an efficiency core inside the background worker* |
| Memory | Instruments Allocations | < 6 MB, flat over a 1 h run |
| Loudness accuracy | EBU Tech 3341 test signals + comparison with Live / Youlean | ±0.3 LU |
| Model behaviour | Unit tests + **debug time-warp** (model clock ×60: a 70 min session runs in 70 s) | Matches the tables in §4 |

---

## 9. Milestones (step 3)

Each milestone has the same rhythm:
1. Claude writes the code and explains it.
2. You build, load it in Live and test.
3. The gate passes.
4. Commit.

| # | Milestone | You'll see / test | Gate |
|---|---|---|---|
| **M0** ✅ | Setup: brew cmake/ninja, git + private GitHub, CMake skeleton (VST3/AU/Standalone), pure pass-through with the 64-bit path, pluginval | Empty brEARk loads in Live | **Null test −∞**, pluginval passes. *Automated gates all green 2026-10-10; Live null test by Kilian* |
| **M1** | Analysis engine: Brain, FIFO, worker, LUFS, FFT features, export detection (+ debug UI with raw numbers) | Live numbers in a debug view | LUFS ±0.3 LU, CPU unmeasurable, Live 11 export flag verified. *Automated: 95 checks incl. EBU Tech 3341 at 44.1–192 kHz, end-to-end Brain test, null test 24/24, pluginval 10, auval. Pending: Live check by Kilian* |
| **M2** | Exposure model + persistence: battery, dose, recovery, silence, day rollover, presets, time-warp | The battery drains and recovers | Unit tests, behaviour matches §4 tables. *Automated: 166 checks (drain table, recovery curve, WHO dose, rollover, weekly, persistence, save/restore), null test 24/24, pluginval 10, auval. Dev tools redirect state via `BREARK_STATE_FILE`. Pending: Live check by Kilian* |
| **M3** | Factors: harshness, PSR, hybrid repetition, time of day, level creep | Factor readouts react to test material | Factors behave on your own loops. *Automated: 188 checks incl. synthetic loops, fader move, drone, playhead loop brace and restarts, night ramp, creep. CPU: loop detection worst case 0.006 % of one core. Pending: Live check by Kilian* |
| **M4** | Break controller + alerts: strictness modes, escalation, BREAK button, toast window, mp3 via system output, recording guard | Full escalation run (time-warped) | Toast never steals focus; alerts never in exports. *Automated: BreakController tests for every rung of the ladder (GENTLE/ESCALATING/HARD), recording guard, silence pausing the clock, re-arm at 30 %, guided break (2036 checks); null test, pluginval (no leaks), auval. Toast: JUCE `windowIgnoresKeyPresses`, so it can never become key; pop-up level; all Spaces and full screen. Sound: `AVAudioPlayer` on the embedded mp3 (1.5 s, −24 dBFS RMS). Pending: Live check by Kilian* |
| **M5** | Hard mode: PowerDown DSP, minimum break, snooze, export bypass | Vinyl power-down/up by ear | Export nulls; pluginval; no clicks. *Automated: PowerDown tests (untouched while plugged in, 32/64-bit; spin-down to exact zeros; click detector mutation-checked: catches a missing 5 ms crossfade; spin-up back to bit-exact; waits for sound; exports untouched while muted; plain-fade fallback; buffer handover). BreakController: minimum break, snooze exactly 10 min, escape by leaving HARD, ladder again if still drained (2071 checks). pluginval with HARD selected: no leaks. Pending: by ear + export test in Live* |
| **M6** | Coach: line format, triggers, tone shift, no-repeat, user file, ~150 lines | The coach talks | Lines fit the UI; no repeats within a session. *Built: 177 lines in 33 sections (`assets/coach/lines.txt`, embedded). Tone = witty (battery > 50 %) / mentor / brutal (brutal alerts). Alerts get coach lines; breaks rotate task/care/fact every minute; hints are silent pop-ups (loud, creep, loop, late, harsh, squashed, dose 50/100), max one per 20 min, each with its own cooldown (squashed: 3 h). User file `coach.txt` adds, `!` replaces, reloaded within ~10 s; `coach.example.txt` template. Tests: coverage per event/tone, placeholders, ≤ 104 chars, shuffle bag, hint rate limits, break rotation (2488 checks)* |
| **M7** | Final UI: brutalist COMPACT / POSTER, fonts, history overlay | The real brEARk | 4 Hz refresh; looks right at both sizes. *Built: design B chosen from mockups. State words FRESH. / OK. / TIRING. / BREAK. / STOP. / RESTING. / UNPLUGGED. / INACTIVE.; the big word is placed by cap height. Settings are cycling labels; POSTER/COMPACT is saved per instance. POSTER: stats row (dose today, week, listened, since break, level, loudness) + factor line + HISTORY overlay (today's battery curve per minute, saved in state.json; 7 days of WHO dose). DATA keeps the debug view and switches. Toasts restyled. `brEARk_RenderUI` renders all states + the real editor to PNG. Tests 2519 checks, null test 24/24, pluginval 10, auval. Pending: Live check by Kilian* |
| **M8** | Hardening: sample-rate/buffer changes, multi-instance, 3 h soak test, Instruments, universal build | — | All gates in §8 green |

**What I need from you:**
- **Before M4:** `alert.mp3`, ideally under 3 s, normalized to a comfortable level.
- **Before M7:** the Space Grotesk / Space Mono font files from Google Fonts. ✅ Fetched (OFL); the Bold was generated from the variable font, see `assets/fonts/README.md`.
