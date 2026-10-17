cmake_minimum_required(VERSION 3.22)

project(Benchmark VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Universal binary on macOS unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

option(BENCHMARK_BUILD_TESTS "Build the unit tests" ON)
option(BENCHMARK_COPY_PLUGIN_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(BENCHMARK_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BENCHMARK_FORMATS AU)
endif()

juce_add_plugin(Benchmark
    PRODUCT_NAME "Benchmark"
    COMPANY_NAME "Citosoft"
    COMPANY_WEBSITE "https://www.citosoft.co.uk"
    BUNDLE_ID "com.citosoft.benchmark"
    PLUGIN_MANUFACTURER_CODE Cito
    PLUGIN_CODE Bnch
    FORMATS ${BENCHMARK_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${BENCHMARK_COPY_PLUGIN_AFTER_BUILD})

# All project sources live in an INTERFACE library so the plugin and the test
# runner compile exactly the same code.
add_library(benchmark_shared INTERFACE)
include(cmake/Sources.cmake)
target_sources(benchmark_shared INTERFACE ${BENCHMARK_SOURCES})
target_include_directories(benchmark_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(benchmark_shared INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
    BENCHMARK_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(benchmark_shared INTERFACE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp)

target_link_libraries(Benchmark
    PRIVATE
        benchmark_shared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BENCHMARK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
