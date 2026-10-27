include(FetchContent)

# Pinned versions. Set FETCHCONTENT_SOURCE_DIR_JUCE to use a local checkout.
set(BENCHMARK_JUCE_TAG   "8.0.15" CACHE STRING "JUCE git tag")
set(BENCHMARK_CATCH2_TAG "v3.16.1" CACHE STRING "Catch2 git tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${BENCHMARK_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

if(BENCHMARK_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${BENCHMARK_CATCH2_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
