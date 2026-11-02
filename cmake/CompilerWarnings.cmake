# Strict warnings for our own non-JUCE targets (engine, tests).
# Plugin targets use juce::juce_recommended_warning_flags instead, because
# JUCE module sources are compiled inside those targets.
function(seam_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    endif()
endfunction()
