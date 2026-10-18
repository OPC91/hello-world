add_executable(BenchmarkTests
    CrossfaderTests.cpp
    GainMatchTests.cpp
    HandoffTests.cpp
    LoudnessTests.cpp
    ProcessorTests.cpp
    ReferencePlayerTests.cpp
    ReferenceTests.cpp
    ResamplerTests.cpp
    UiSnapshot.cpp)

target_link_libraries(BenchmarkTests
    PRIVATE
        benchmark_shared
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# The test runner is a plain executable, not a plugin. Modal loops let tests
# pump the message queue while waiting for background loads.
target_compile_definitions(BenchmarkTests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

include(Catch)
catch_discover_tests(BenchmarkTests DISCOVERY_MODE PRE_TEST)
