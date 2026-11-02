set(SEAM_COMPANY_NAME     "Exnilo")
set(SEAM_MANUFACTURER     "Exnl")
set(SEAM_BUNDLE_ID_PREFIX "uk.co.exnilo")

juce_add_plugin(SEAM
    PRODUCT_NAME                "SEAM"
    COMPANY_NAME                "${SEAM_COMPANY_NAME}"
    COMPANY_WEBSITE             "https://exnilo.co.uk"
    BUNDLE_ID                   "${SEAM_BUNDLE_ID_PREFIX}.seam"
    PLUGIN_MANUFACTURER_CODE    ${SEAM_MANUFACTURER}
    PLUGIN_CODE                 Seam
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics
    VERSION                     ${PROJECT_VERSION})

target_sources(SEAM PRIVATE
    PluginProcessor.h PluginProcessor.cpp
    PluginEditor.h PluginEditor.cpp
    Parameters.h Parameters.cpp
    FillWorker.h FillWorker.cpp
    FillComponents.h FillComponents.cpp
    Presets.h Presets.cpp)

target_include_directories(SEAM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_definitions(SEAM PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0)

target_link_libraries(SEAM
    PRIVATE
        seam_engine
        exnilo_ui
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
