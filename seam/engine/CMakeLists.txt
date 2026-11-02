# The SEAM engine: all DSP, phrase tracking, slicing and fill generation. Pure C++20, never JUCE,
# so it is unit-testable without a host (enforced by the engine_is_juce_free test).
add_library(seam_engine STATIC
    PhraseTracker.h PhraseTracker.cpp
    SeamEngine.h SeamEngine.cpp
    TransitionChain.h TransitionChain.cpp
    dsp/Svf.h
    dsp/ThrowReverb.h dsp/ThrowReverb.cpp)
target_include_directories(seam_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(seam_engine PUBLIC exnilo_rt)
seam_set_warnings(seam_engine)
