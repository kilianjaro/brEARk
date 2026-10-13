cmake_minimum_required(VERSION 3.22)

# Must be set before project(): minimum macOS for Apple-Silicon-native Live 11.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(brEARk VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── JUCE ──────────────────────────────────────────────────────────────────────
# Pinned to 8.0.13. To use a local checkout instead of downloading:
#   cmake ... -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ── Plugin ────────────────────────────────────────────────────────────────────
juce_add_plugin(brEARk
    PRODUCT_NAME                "brEARk"
    COMPANY_NAME                "Totemphonia Studio Berlin"
    BUNDLE_ID                   "com.totemphonia.brEARk"
    PLUGIN_MANUFACTURER_CODE    Totm
    PLUGIN_CODE                 Brek
    DESCRIPTION                 "Listening-fatigue coach for the master channel"
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     TRUE)

# Engine sources are shared by the plugin and the unit tests.
set(BREARK_ENGINE_SOURCES
    source/dsp/PowerDown.cpp
    source/engine/Brain.cpp
    source/engine/BreakController.cpp
    source/engine/ExposureModel.cpp
    source/engine/FatigueFactors.cpp
    source/engine/LoudnessMeter.cpp
    source/engine/Persistence.cpp
    source/engine/GridLoopDetector.cpp
    source/engine/SpectrumAnalyser.cpp)

option(BREARK_DEV_METRICS "Measure audio-thread time per block (shown in the debug view)" ON)

# Everything else in the plugin (also built into the UI render tool).
set(BREARK_PLUGIN_SOURCES
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/DebugView.cpp
    source/AlertPresenter.cpp
    source/coach/Coach.cpp
    source/coach/CoachLines.cpp
    source/ui/ToastWindow.cpp
    source/ui/Look.cpp
    source/ui/MeterView.cpp
    source/ui/HistoryView.cpp
    source/platform/AlertSound_mac.mm
    source/platform/WindowTweaks_mac.mm)

target_sources(brEARk PRIVATE
    ${BREARK_PLUGIN_SOURCES}
    ${BREARK_ENGINE_SOURCES})

# The alert sound, embedded in the plugin binary (BinaryData::alert_mp3).
juce_add_binary_data(breark_assets SOURCES
    assets/alert.mp3
    assets/coach/lines.txt
    assets/fonts/SpaceGrotesk-Bold.ttf
    assets/fonts/SpaceMono-Regular.ttf
    assets/fonts/SpaceMono-Bold.ttf)

target_include_directories(brEARk PRIVATE source)

# Build ID (git commit + time) shown in the debug view.
set(BREARK_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
add_custom_target(breark_build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${BREARK_GENERATED_DIR}/BuildInfo.h
            -P ${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS ${BREARK_GENERATED_DIR}/BuildInfo.h)
add_dependencies(brEARk breark_build_info)
target_include_directories(brEARk PRIVATE ${BREARK_GENERATED_DIR})

target_compile_definitions(brEARk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    BREARK_DEV_METRICS=$<BOOL:${BREARK_DEV_METRICS}>)

target_link_libraries(brEARk
    PRIVATE
        breark_assets
        juce::juce_audio_utils
        juce::juce_dsp
        "-framework AVFoundation"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ── Developer tools ───────────────────────────────────────────────────────────
option(BREARK_BUILD_TOOLS "Build developer tools (unit tests, null test)" ON)

if(BREARK_BUILD_TOOLS)
    enable_testing()

    # Engine unit tests: BS.1770 / EBU Tech 3341, A-weighting, FIFO.
    juce_add_console_app(brEARk_Tests PRODUCT_NAME "brEARk_Tests")

    target_sources(brEARk_Tests PRIVATE
        tests/TestMain.cpp
        tests/LoudnessTests.cpp
        tests/SpectrumTests.cpp
        tests/FifoTests.cpp
        tests/BrainTests.cpp
        tests/PerformanceTests.cpp
        tests/ExposureModelTests.cpp
        tests/PersistenceTests.cpp
        tests/FactorTests.cpp
        tests/LoopDetectorTests.cpp
        tests/BreakControllerTests.cpp
        tests/PowerDownTests.cpp
        tests/CoachTests.cpp
        tests/MeterModelTests.cpp
        tests/HostActivityTests.cpp
        tests/RealFftTests.cpp
        tests/CheckInTests.cpp
        source/coach/Coach.cpp
        source/coach/CoachLines.cpp
        source/ui/Look.cpp
        source/ui/MeterView.cpp
        ${BREARK_ENGINE_SOURCES})

    target_include_directories(brEARk_Tests PRIVATE source)

    target_compile_definitions(brEARk_Tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        BREARK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    target_link_libraries(brEARk_Tests
        PRIVATE
            breark_assets
            juce::juce_gui_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME brEARk_Tests COMMAND brEARk_Tests)

    # Renders the UI (every state, both sizes, history, pop-ups) to PNG files.
    juce_add_console_app(brEARk_RenderUI PRODUCT_NAME "brEARk_RenderUI")

    target_sources(brEARk_RenderUI PRIVATE
        tools/RenderUI.cpp
        ${BREARK_PLUGIN_SOURCES}
        ${BREARK_ENGINE_SOURCES})

    target_include_directories(brEARk_RenderUI PRIVATE source ${BREARK_GENERATED_DIR})
    add_dependencies(brEARk_RenderUI breark_build_info)

    target_compile_definitions(brEARk_RenderUI PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="brEARk"
        BREARK_DEV_METRICS=1)

    target_link_libraries(brEARk_RenderUI
        PRIVATE
            breark_assets
            juce::juce_audio_utils
            juce::juce_dsp
            "-framework AVFoundation"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Interposes allocation, locks, waits and I/O to catch them on the audio thread.
    add_library(brEARk_RealtimeCheck SHARED tools/RealtimeCheck.cpp)
    set_target_properties(brEARk_RealtimeCheck PROPERTIES
        OUTPUT_NAME brEARk_RealtimeCheck
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/brEARk_Soak_artefacts/$<CONFIG>")

    # Hours of realtime operation: device changes, switch-offs, stalls,
    # instances coming and going, HARD power-downs; memory and realtime checks.
    juce_add_console_app(brEARk_Soak PRODUCT_NAME "brEARk_Soak")

    target_sources(brEARk_Soak PRIVATE
        tools/Soak.cpp
        ${BREARK_PLUGIN_SOURCES}
        ${BREARK_ENGINE_SOURCES})

    target_include_directories(brEARk_Soak PRIVATE source ${BREARK_GENERATED_DIR})
    add_dependencies(brEARk_Soak breark_build_info brEARk_RealtimeCheck)

    target_compile_definitions(brEARk_Soak PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="brEARk"
        BREARK_DEV_METRICS=$<BOOL:${BREARK_DEV_METRICS}>)

    target_link_libraries(brEARk_Soak
        PRIVATE
            breark_assets
            juce::juce_audio_utils
            juce::juce_dsp
            "-framework AVFoundation"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Hosts the installed VST3 + AU and proves output == input, bit for bit.
    juce_add_console_app(brEARk_NullTest PRODUCT_NAME "brEARk_NullTest")

    target_sources(brEARk_NullTest PRIVATE tools/NullTest.cpp)

    target_compile_definitions(brEARk_NullTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(brEARk_NullTest
        PRIVATE
            juce::juce_audio_processors_headless
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
