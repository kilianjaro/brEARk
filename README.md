# brEARk.

A plugin for Ableton Live's master channel that listens to everything you hear and tells you **when to stop listening**, protecting your hearing and keeping your ears fresh.

- Concept: [CONCEPT.md](CONCEPT.md)
- Implementation plan & milestones: [IMPLEMENTATION.md](IMPLEMENTATION.md)

## Build (macOS)

Requirements: Xcode command line tools, CMake ≥ 3.22, Ninja (`brew install cmake ninja`).

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The first configure downloads JUCE 8.0.13. The VST3 and AU are copied to `~/Library/Audio/Plug-Ins/` after every build. The Standalone app is in `build/brEARk_artefacts/Release/Standalone/`.

Universal binary (Apple Silicon + Intel) for release builds: add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

## Verify

```bash
./build/brEARk_Tests_artefacts/Release/brEARk_Tests
./build/brEARk_NullTest_artefacts/Release/brEARk_NullTest
BREARK_STATE_FILE=/tmp/breark-auval.json auval -v aufx Brek Totm
BREARK_STATE_FILE=/tmp/breark-pluginval.json /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/VST3/brEARk.vst3
```

`brEARk_Tests` checks the engine (BS.1770 / EBU Tech 3341, A-weighting, FIFO, end-to-end Brain, CPU budget). `brEARk_NullTest` hosts the installed VST3 and AU like a DAW and requires the output to be **bit-identical** to the input (mono/stereo, 44.1–192 kHz, 32/64-bit, irregular block sizes). Both tools redirect the listening history (`BREARK_STATE_FILE`), so testing never touches your real `~/Library/Application Support/brEARk/state.json`.
