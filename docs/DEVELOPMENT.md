# brEARk: build and verify

For developers. What brEARk is: [README.md](../README.md).

- What it does and every alert: [ALERTS.md](ALERTS.md)
- Changes per version: [CHANGELOG.md](../CHANGELOG.md)

Code comments tagged M1 to M11 refer to the development milestones before 1.0; the git history has the details.

## Build (macOS)

Requirements: Xcode command line tools, CMake ≥ 3.22, Ninja (`brew install cmake ninja`).

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The first configure downloads JUCE 8.0.13. The VST3 and AU are copied to `~/Library/Audio/Plug-Ins/` after every build. The Standalone app is in `build/brEARk_artefacts/Release/Standalone/`.

Universal binary (Apple Silicon + Intel) for release builds, in its own folder:

```bash
cmake -B build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-universal
```

It installs over the arm64 build in `~/Library/Audio/Plug-Ins/`. The Intel half can be tested on Apple Silicon through Rosetta by prefixing the tools below with `arch -x86_64`.

## Verify

```bash
./build/brEARk_Tests_artefacts/Release/brEARk_Tests
./build/brEARk_NullTest_artefacts/Release/brEARk_NullTest
BREARK_STATE_FILE=/tmp/breark-auval.json auval -v aufx Brek Totm
BREARK_STATE_FILE=/tmp/breark-pluginval.json /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/VST3/brEARk.vst3
BREARK_STATE_FILE=/tmp/breark-pluginval.json /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/Components/brEARk.component
./build/brEARk_Soak_artefacts/Release/brEARk_Soak 180 soak-results
```

`brEARk_Tests [name]` runs all tests, or only those whose name contains `name`.

`brEARk_Tests` checks the engine (BS.1770 / EBU Tech 3341, A-weighting, FIFO, end-to-end Brain, CPU budget). `brEARk_NullTest` hosts the installed VST3 and AU like a DAW and requires the output to be **bit-identical** to the input (mono/stereo, 44.1–192 kHz, 32/64-bit, irregular block sizes, switched off and on mid-stream).

`brEARk_Soak [minutes] [folder] [quick]` runs brEARk in real time like Live would, for 3 h by default. First it opens, paints and closes the editor 100 times. Then, every ~8 min, it changes the audio device (rate, block size, 32/64-bit, mono/stereo), goes silent, is switched off, loses its audio, and swaps instances. Every ~hour it also runs a time-warped HARD power-down. It checks:
- bit-exact output;
- **no allocation, lock or I/O on the audio thread** (it re-launches itself with `libbrEARk_RealtimeCheck.dylib` inserted);
- dropped audio under 1 %;
- engine memory under 6 MB;
- memory flat hour by hour, and an editor that doesn't grow;
- that every state was reached.

It writes `soak.csv` (one row per 10 s). `quick` shortens the phases to a tenth for a smoke test. No pop-ups or sounds (`BREARK_QUIET`). The Intel half runs under Rosetta with `arch -x86_64 -e DYLD_INSERT_LIBRARIES=<path>/libbrEARk_RealtimeCheck.dylib <path>/brEARk_Soak 25`. `arch` is a system tool, so macOS strips `DYLD_` variables passed any other way.

`brEARk_Soak showcase [folder]` makes the README screenshots (about 4 minutes): a scripted day plus 2.5 minutes of real-time listening to a rendered house loop, saved as `showcase-poster.png`, `showcase-history.png` and `showcase-compact.png`; then a break (the music stops, BREAK is pressed, a minute later the coach shows a thought on the track: `showcase-break.png`); then the NEW SESSION. and BREAK. pop-ups side by side (`showcase-popups.png`; they flash on screen while being drawn). Run it as `brEARk_Soak showcase docs/images`.

`brEARk_DaySim [scenario]` plays whole producer days through the real exposure model, break controller, check-ins and coach, second by second, with the plugin's pop-up timing, and prints every pop-up, sound and question plus pop-ups per listening hour. Five scenarios: a disciplined day, a hyperfocus day in ESCALATING and in HARD, a GENTLE night owl, and a day with five Live restarts. Use it to see what a change does to how often brEARk speaks.

The GitHub workflow (`.github/workflows/build.yml`) builds the universal plugin and runs the unit tests on every push and pull request.

All tools redirect the listening history (`BREARK_STATE_FILE`), so testing never touches your real `~/Library/Application Support/brEARk/state.json`.

## Release

`tools/release.sh` builds a release from scratch in `build-release/`: universal binaries without TEST MODE, the unit tests, then an installer package in `dist/` with its SHA-256. The package installs the VST3 and the AU for all users (`/Library/Audio/Plug-Ins/`). Developer builds go to `~/Library/Audio/Plug-Ins/`; remove those before testing an installed release, or Live sees two copies.

Signed and notarized (no Gatekeeper warning) it needs the Apple Developer Program and, once:

1. In Xcode › Settings › Accounts › Manage Certificates, create a **Developer ID Application** and a **Developer ID Installer** certificate.
2. Create an app-specific password at appleid.apple.com and store it for notarytool:
   `xcrun notarytool store-credentials breark-notary --apple-id <your Apple ID> --team-id <TEAMID>`
3. Run:

```bash
BREARK_SIGN_APP="Developer ID Application: <Name> (<TEAMID>)" BREARK_SIGN_INSTALLER="Developer ID Installer: <Name> (<TEAMID>)" BREARK_NOTARY_PROFILE=breark-notary tools/release.sh
```

Without these variables the script builds `brEARk-<version>-macOS-UNSIGNED.pkg`, for testing only. `BREARK_JUCE_DIR=<path>` reuses an existing JUCE 8.0.13 checkout instead of downloading it.

After a release: upload the `.pkg` and its `.sha256` to a GitHub release tagged `v<version>`, and add the changes to `CHANGELOG.md`.

## Your own coach lines

Put a `coach.txt` next to the listening history in `~/Library/Application Support/brEARk/`; `coach.example.txt` in the same folder is a template. The format is the same as [assets/coach/lines.txt](../assets/coach/lines.txt):
- Your lines are **added** to the built-in ones.
- A section header ending in `!` (e.g. `[idle/witty!]`) **replaces** that section instead.
- Changes are picked up within about 10 s.
