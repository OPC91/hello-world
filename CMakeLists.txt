cmake_minimum_required(VERSION 3.22)

project(BreakForge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

option(BREAKFORGE_BUILD_TESTS "Build the unit tests" ON)
option(BREAKFORGE_COPY_PLUGIN "Copy the built plugin into the system plug-in folder" OFF)

# ------------------------------------------------------------------------------
# Dependencies. Override with -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE (and
# FETCHCONTENT_SOURCE_DIR_CATCH2) to build against local checkouts.
# ------------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------------------------
# Plugin
# ------------------------------------------------------------------------------
set(BREAKFORGE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BREAKFORGE_FORMATS AU)
endif()

juce_add_plugin(BreakForge
    COMPANY_NAME                "BreakForge Audio"
    COMPANY_WEBSITE             "https://github.com/opc91/hello-world"
    BUNDLE_ID                   com.breakforgeaudio.breakforge
    PLUGIN_MANUFACTURER_CODE    Bfrg
    PLUGIN_CODE                 Bfg1
    FORMATS                     ${BREAKFORGE_FORMATS}
    PRODUCT_NAME                "BreakForge"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${BREAKFORGE_COPY_PLUGIN}
    VST3_CATEGORIES             Instrument Drum Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

# Engine sources are shared between the plugin and the test runner.
set(BREAKFORGE_CORE_SOURCES
    Source/Core/AudioFileLoader.cpp
    Source/Core/BuiltInKits.cpp
    Source/Core/DrumSynth.cpp
    Source/Core/Exporter.cpp
    Source/Core/FactoryPatterns.cpp
    Source/Core/Engine.cpp
    Source/Core/GrooveEngine.cpp
    Source/Core/KitLibrary.cpp
    Source/Core/MasterBus.cpp
    Source/Core/Params.cpp
    Source/Core/Pattern.cpp
    Source/Core/PatternGenerator.cpp
    Source/Core/PatternTools.cpp
    Source/Core/SampleManager.cpp
    Source/Core/Scheduler.cpp
    Source/Core/StateIO.cpp
    Source/Core/UserLibrary.cpp
    Source/Core/Voice.cpp)

set(BREAKFORGE_PLUGIN_SOURCES
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/UI/ExportHandle.cpp
    Source/UI/PianoRoll.cpp
    Source/UI/StepGrid.cpp
    Source/UI/StepPopup.cpp
    Source/UI/Theme.cpp
    Source/UI/TopBar.cpp
    Source/UI/TrackHeader.cpp
    Source/UI/TrackPanel.cpp)

target_sources(BreakForge PRIVATE ${BREAKFORGE_CORE_SOURCES} ${BREAKFORGE_PLUGIN_SOURCES})
target_include_directories(BreakForge PRIVATE Source)

target_compile_definitions(BreakForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BreakForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(BREAKFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
