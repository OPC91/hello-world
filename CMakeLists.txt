cmake_minimum_required(VERSION 3.22)

project(SEAM VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Single-config generators (Ninja, Makefiles) default to Release.
get_property(SEAM_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT SEAM_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SEAM_BUILD_TESTS "Build the Catch2 engine tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

# Exnilo shared code: used by every Exnilo plugin.
#   exnilo_rt  - header-only, JUCE-free realtime helpers (queues, exchange, PRNG, DSP building blocks)
#   exnilo_ui  - JUCE module: theme, LookAndFeel, hero knob, energy display, widgets, presets, licensing
add_library(exnilo_rt INTERFACE)
target_include_directories(exnilo_rt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/shared/exnilo_rt")
juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}/shared/exnilo_ui")

add_subdirectory(seam/engine)
add_subdirectory(seam/plugin)

if(SEAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
