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
    source/engine/Brain.cpp
    source/engine/LoudnessMeter.cpp
    source/engine/SpectrumAnalyser.cpp)

option(BREARK_DEV_METRICS "Measure audio-thread time per block (shown in the debug view)" ON)

target_sources(brEARk PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    ${BREARK_ENGINE_SOURCES})

target_include_directories(brEARk PRIVATE source)

target_compile_definitions(brEARk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    BREARK_DEV_METRICS=$<BOOL:${BREARK_DEV_METRICS}>)

target_link_libraries(brEARk
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
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
        ${BREARK_ENGINE_SOURCES})

    target_include_directories(brEARk_Tests PRIVATE source)

    target_compile_definitions(brEARk_Tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(brEARk_Tests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME brEARk_Tests COMMAND brEARk_Tests)

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
