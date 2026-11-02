# Engine tests link only seam_engine (never JUCE).
add_executable(SeamTests
    TestHelpers.h
    PhraseTrackerTests.cpp
    TransitionTests.cpp)
target_link_libraries(SeamTests PRIVATE seam_engine Catch2::Catch2WithMain)
seam_set_warnings(SeamTests)

add_test(NAME engine_tests COMMAND SeamTests)
add_test(NAME engine_is_juce_free
    COMMAND ${CMAKE_COMMAND} -DENGINE_DIR=${PROJECT_SOURCE_DIR}/seam/engine
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckNoJuce.cmake)

# Offscreen render of the editor (themes, scales, states) to PNGs: a UI smoke test and a way to review the UI
# without opening a window. Borrows the plugin's shared-code target.
if(TARGET SEAM)
    juce_add_console_app(SeamUiSnapshot PRODUCT_NAME "SeamUiSnapshot")
    target_sources(SeamUiSnapshot PRIVATE ui/UiSnapshot.cpp)
    target_include_directories(SeamUiSnapshot PRIVATE "${PROJECT_SOURCE_DIR}/seam" "${PROJECT_SOURCE_DIR}/seam/plugin"
                               $<TARGET_PROPERTY:SEAM,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SeamUiSnapshot PRIVATE $<TARGET_PROPERTY:SEAM,COMPILE_DEFINITIONS>)
    target_link_libraries(SeamUiSnapshot PRIVATE SEAM seam_engine)
    add_test(NAME ui_snapshot COMMAND SeamUiSnapshot "${CMAKE_CURRENT_BINARY_DIR}/ui-snapshots")
endif()
