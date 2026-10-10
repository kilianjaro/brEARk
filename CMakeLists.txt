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

target_sources(brEARk PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_compile_definitions(brEARk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(brEARk
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ── Developer tools ───────────────────────────────────────────────────────────
option(BREARK_BUILD_TOOLS "Build developer tools (null test)" ON)

if(BREARK_BUILD_TOOLS)
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
