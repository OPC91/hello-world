# Third-party dependencies, pinned to exact tags for reproducible builds.
# To build against a local checkout instead of downloading, pass e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/path/to/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/path/to/Catch2
include(FetchContent)

set(SEAM_JUCE_TAG   "8.0.15"  CACHE STRING "JUCE git tag")
set(SEAM_CATCH2_TAG "v3.16.1" CACHE STRING "Catch2 git tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${SEAM_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

if(SEAM_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${SEAM_CATCH2_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()
