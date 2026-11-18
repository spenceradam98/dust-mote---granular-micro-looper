cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; build universal (x86_64 + arm64) on CI.
# No -march flags anywhere: the DSP is scalar C++ and runs on any x86-64 (no AVX2 needed).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()
project(DustMote VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core engine (no JUCE dependency, unit-testable) ----
add_library(dustmote_engine STATIC Source/engine/Engine.cpp)
target_include_directories(dustmote_engine PUBLIC Source/engine)
set_target_properties(dustmote_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(DUSTMOTE_BUILD_TESTS "Build engine tests" ON)
if(DUSTMOTE_BUILD_TESTS)
    add_executable(dustmote_tests tests/EngineTests.cpp)
    target_link_libraries(dustmote_tests PRIVATE dustmote_engine)
    enable_testing()
    add_test(NAME engine COMMAND dustmote_tests)
endif()

option(DUSTMOTE_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(NOT DUSTMOTE_BUILD_PLUGIN)
    return()
endif()

# ---- JUCE ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(DUSTMOTE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DUSTMOTE_FORMATS AU)
endif()

juce_add_plugin(DustMote
    COMPANY_NAME "Dust Mote Audio"
    PLUGIN_MANUFACTURER_CODE Dstm
    PLUGIN_CODE Dmt1
    BUNDLE_ID com.spenceradam98.dustmote
    FORMATS ${DUSTMOTE_FORMATS}
    PRODUCT_NAME "Dust Mote"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx Delay)

target_sources(DustMote PRIVATE
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/Themes.cpp)

target_compile_definitions(DustMote PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DustMote
    PRIVATE
        dustmote_engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Screenshot tool (renders docs/screenshots without a host) ----
option(DUSTMOTE_BUILD_TOOLS "Build the snapshot tool" OFF)
if(DUSTMOTE_BUILD_TOOLS)
    juce_add_console_app(Snapshots PRODUCT_NAME "Snapshots")
    target_sources(Snapshots PRIVATE tools/Snapshots.cpp
        Source/plugin/PluginProcessor.cpp Source/plugin/PluginEditor.cpp Source/plugin/Themes.cpp)
    target_include_directories(Snapshots PRIVATE Source/plugin)
    target_compile_definitions(Snapshots PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Dust Mote")
    target_link_libraries(Snapshots PRIVATE dustmote_engine juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endif()
