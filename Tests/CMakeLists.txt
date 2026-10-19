FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(Catch2)

juce_add_console_app(BreakForgeTests PRODUCT_NAME "BreakForgeTests")

list(TRANSFORM BREAKFORGE_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE BREAKFORGE_TEST_CORE_SOURCES)
list(TRANSFORM BREAKFORGE_PLUGIN_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE BREAKFORGE_TEST_PLUGIN_SOURCES)

target_sources(BreakForgeTests PRIVATE
    ${BREAKFORGE_TEST_CORE_SOURCES}
    ${BREAKFORGE_TEST_PLUGIN_SOURCES}
    PluginStateTests.cpp
    EngineTests.cpp
    ExportTests.cpp
    GeneratorTests.cpp
    GrooveTests.cpp
    KitTests.cpp
    SampleTests.cpp
    SchedulerTests.cpp)

target_include_directories(BreakForgeTests PRIVATE "${PROJECT_SOURCE_DIR}/Source")

# The processor is compiled in directly (not as a plugin), so provide the plugin macros it uses.
target_compile_definitions(BreakForgeTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BreakForge"
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(BreakForgeTests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
)

include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
catch_discover_tests(BreakForgeTests)
