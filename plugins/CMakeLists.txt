# Two plugins: "Raumklang Quelle" (one per track) and "Raumklang Raum" (master bus).
set(RK_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RK_PLUGIN_FORMATS AU)
endif()

function(rk_add_plugin target name code description)
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "Raumklang"
        BUNDLE_ID "io.github.benjaminjmueller.${target}"
        PLUGIN_MANUFACTURER_CODE Rmkl
        PLUGIN_CODE ${code}
        DESCRIPTION "${description}"
        FORMATS ${RK_PLUGIN_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx Spatial Surround
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_link_libraries(${target} PRIVATE
        rk_shared rk_juce_defaults
        juce::juce_audio_utils juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags rk_warnings)
    target_compile_definitions(${target} PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
endfunction()

rk_add_plugin(RaumklangQuelle "Raumklang Quelle" Rkq1 "Places a track in the room (spatial panner)")
target_sources(RaumklangQuelle PRIVATE source/SourceProcessor.cpp source/SourceEditor.cpp)

rk_add_plugin(RaumklangRaum "Raumklang Raum" Rkr1 "Speaker setup, calibration and headphone preview")
target_sources(RaumklangRaum PRIVATE room/RoomProcessor.cpp room/RoomEditor.cpp)

if(RK_BUILD_TESTS)
    juce_add_console_app(rk_plugin_tests PRODUCT_NAME "Raumklang Plugin Tests")
    target_sources(rk_plugin_tests PRIVATE tests/PluginStressTest.cpp
        source/SourceProcessor.cpp source/SourceEditor.cpp room/RoomProcessor.cpp room/RoomEditor.cpp)
    target_compile_definitions(rk_plugin_tests PRIVATE RK_NO_PLUGIN_ENTRY=1 JucePlugin_Name="Raumklang Test"
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(rk_plugin_tests PRIVATE rk_shared
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_test(NAME rk_plugin_tests COMMAND rk_plugin_tests)
endif()
