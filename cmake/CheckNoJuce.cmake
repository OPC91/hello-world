# Run as a test: cmake -DENGINE_DIR=<path> -P CheckNoJuce.cmake
# Fails if any engine source mentions JUCE. The engine target has no JUCE
# include paths, so a JUCE include would also fail to compile; this also
# catches juce:: names and JUCE_ macros that would only break later.
file(GLOB_RECURSE engine_files "${ENGINE_DIR}/*.h" "${ENGINE_DIR}/*.cpp")
set(offenders "")
foreach(f IN LISTS engine_files)
    file(STRINGS "${f}" hits REGEX "#[ \t]*include[ \t]*[<\"]juce|juce::|JUCE_")
    if(hits)
        list(APPEND offenders "${f}")
    endif()
endforeach()
if(offenders)
    message(FATAL_ERROR "Engine sources must not depend on JUCE:\n${offenders}")
endif()
message(STATUS "Engine is JUCE-free (${CMAKE_CURRENT_LIST_FILE})")
