# CI passes its run number; local builds stay 0 and never update themselves.
set(RK_BUILD_NUMBER "0" CACHE STRING "Build number (CI run number) for updates")

juce_add_gui_app(RaumklangApp
    PRODUCT_NAME "Raumklang"
    COMPANY_NAME "Raumklang"
    BUNDLE_ID "io.github.benjaminjmueller.raumklang"
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${RK_BUILD_NUMBER}
    ICON_BIG "${CMAKE_SOURCE_DIR}/packaging/icon/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Raumklang verteilt die Eingänge deines Audio-Interfaces auf die Lautsprecher."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

target_sources(RaumklangApp PRIVATE Main.cpp MainComponent.cpp AudioHost.cpp Calibration.cpp MicPermission.cpp Updater.cpp PluginInstaller.cpp)
if(APPLE)
    target_sources(RaumklangApp PRIVATE MicPermission.mm)
    target_link_libraries(RaumklangApp PRIVATE "-framework AVFoundation")
endif()
target_compile_definitions(RaumklangApp PRIVATE
    RK_BUILD_NUMBER=${RK_BUILD_NUMBER}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RaumklangApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RaumklangApp,JUCE_VERSION>")
target_link_libraries(RaumklangApp PRIVATE
    rk_shared rk_juce_defaults
    juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_extra juce::juce_cryptography
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags rk_warnings)
