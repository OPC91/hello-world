# Every translation unit in the project, grouped by module.
set(BENCHMARK_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)

set(BENCHMARK_SOURCES
    # Plugin wiring
    ${BENCHMARK_SOURCE_DIR}/Parameters.cpp
    ${BENCHMARK_SOURCE_DIR}/PluginProcessor.cpp
    ${BENCHMARK_SOURCE_DIR}/PluginEditor.cpp

    # Real-time DSP
    ${BENCHMARK_SOURCE_DIR}/dsp/Crossfader.cpp
    ${BENCHMARK_SOURCE_DIR}/dsp/GainMatcher.cpp
    ${BENCHMARK_SOURCE_DIR}/dsp/KWeighting.cpp
    ${BENCHMARK_SOURCE_DIR}/dsp/LoudnessMeter.cpp
    ${BENCHMARK_SOURCE_DIR}/dsp/ReferencePlayer.cpp

    # Reference files (background threads)
    ${BENCHMARK_SOURCE_DIR}/reference/ReferenceDecoder.cpp
    ${BENCHMARK_SOURCE_DIR}/reference/ReferenceLibrary.cpp
    ${BENCHMARK_SOURCE_DIR}/reference/ReferenceLoader.cpp
    ${BENCHMARK_SOURCE_DIR}/reference/Resampler.cpp

    # UI
    ${BENCHMARK_SOURCE_DIR}/ui/ABToggle.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/LoudnessPanel.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/PlaceholderPanel.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/ReferenceListPanel.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/Theme.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/WaveformView.cpp)
