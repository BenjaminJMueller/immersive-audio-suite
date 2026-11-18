juce_add_gui_app(RaumklangApp
    PRODUCT_NAME "Raumklang"
    COMPANY_NAME "Raumklang"
    BUNDLE_ID "io.github.benjaminjmueller.raumklang"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Raumklang verteilt die Eingänge deines Audio-Interfaces auf die Lautsprecher."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

target_sources(RaumklangApp PRIVATE Main.cpp MainComponent.cpp AudioHost.cpp)
target_compile_definitions(RaumklangApp PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RaumklangApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RaumklangApp,JUCE_VERSION>")
target_link_libraries(RaumklangApp PRIVATE
    rk_shared rk_juce_defaults
    juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags rk_warnings)
