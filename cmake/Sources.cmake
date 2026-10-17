# Every translation unit in the project, grouped by module.
set(BENCHMARK_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)

set(BENCHMARK_SOURCES
    ${BENCHMARK_SOURCE_DIR}/Parameters.cpp
    ${BENCHMARK_SOURCE_DIR}/PluginProcessor.cpp
    ${BENCHMARK_SOURCE_DIR}/PluginEditor.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/Theme.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/ABToggle.cpp
    ${BENCHMARK_SOURCE_DIR}/ui/PlaceholderPanel.cpp)
