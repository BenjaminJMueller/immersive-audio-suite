# libmysofa (BSD-3-Clause) reads SOFA files (AES69) with measured head responses. Only
# its sources are used: zlib comes from JUCE through a small shim (rk_ui/SofaGunzip.cpp),
# so no further library is needed on any platform.
include(FetchContent)
FetchContent_Declare(mysofa
    GIT_REPOSITORY https://github.com/hoene/libmysofa.git
    GIT_TAG v1.3.5
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(mysofa)
set(RK_MYSOFA_SRC ${mysofa_SOURCE_DIR}/src)
set(RK_MYSOFA_GEN ${CMAKE_CURRENT_BINARY_DIR}/mysofa-gen)
file(WRITE ${RK_MYSOFA_GEN}/config.h "#pragma once\n#define CMAKE_INSTALL_PREFIX \"\"\n#define CPACK_PACKAGE_VERSION_MAJOR 1\n#define CPACK_PACKAGE_VERSION_MINOR 3\n#define CPACK_PACKAGE_VERSION_PATCH 5\n")
file(WRITE ${RK_MYSOFA_GEN}/mysofa_export.h "#pragma once\n#define MYSOFA_EXPORT\n")
add_library(rk_mysofa STATIC
    ${RK_MYSOFA_SRC}/hrtf/reader.c
    ${RK_MYSOFA_SRC}/hdf/superblock.c
    ${RK_MYSOFA_SRC}/hdf/dataobject.c
    ${RK_MYSOFA_SRC}/hdf/btree.c
    ${RK_MYSOFA_SRC}/hdf/fractalhead.c
    ${RK_MYSOFA_SRC}/hdf/gcol.c
    ${RK_MYSOFA_SRC}/hrtf/check.c
    ${RK_MYSOFA_SRC}/hrtf/spherical.c
    ${RK_MYSOFA_SRC}/hrtf/lookup.c
    ${RK_MYSOFA_SRC}/hrtf/tools.c
    ${RK_MYSOFA_SRC}/hrtf/kdtree.c
    ${RK_MYSOFA_SRC}/hrtf/neighbors.c
    ${RK_MYSOFA_SRC}/hrtf/interpolate.c
    ${RK_MYSOFA_SRC}/hrtf/resample.c
    ${RK_MYSOFA_SRC}/hrtf/loudness.c
    ${RK_MYSOFA_SRC}/hrtf/minphase.c
    ${RK_MYSOFA_SRC}/hrtf/easy.c
    ${RK_MYSOFA_SRC}/hrtf/cache.c
    ${RK_MYSOFA_SRC}/resampler/speex_resampler.c)
target_include_directories(rk_mysofa PRIVATE ${RK_MYSOFA_GEN} ${RK_MYSOFA_SRC}/hrtf ${RK_MYSOFA_SRC}/resampler)
target_include_directories(rk_mysofa INTERFACE ${RK_MYSOFA_GEN} ${RK_MYSOFA_SRC}/hrtf)
target_compile_definitions(rk_mysofa PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(rk_mysofa PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
if(NOT MSVC)
    target_compile_options(rk_mysofa PRIVATE -w)
    target_link_libraries(rk_mysofa PUBLIC m)
endif()

# UI and document code shared by the app and the plugins. Compiled into each target
# (INTERFACE library) so that it picks up that target's JUCE configuration.
add_library(rk_shared INTERFACE)
target_sources(rk_shared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Strings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/StageView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/RoomView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/ReverbPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Snapshots.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/View3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Visuals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Pages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Survey.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/Sofa.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rk_ui/SofaGunzip.cpp)
target_include_directories(rk_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Interface font (Inter, SIL OFL 1.1), embedded so text looks the same and crisp on every platform.
juce_add_binary_data(rk_fonts NAMESPACE RkFonts HEADER_NAME RkFonts.h SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf)
set_target_properties(rk_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rk_shared INTERFACE rk_fonts)
target_link_libraries(rk_shared INTERFACE rk::core rk_mysofa)

# Common JUCE settings for every Raumklang target.
add_library(rk_juce_defaults INTERFACE)
target_compile_definitions(rk_juce_defaults INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    # Without the stem darkening of "font smoothing" text on Retina screens stays thin and sharp.
    JUCE_DISABLE_COREGRAPHICS_FONT_SMOOTHING=1)

# Headless tests for the scene file format (fuzzed JSON, round trips).
if(RK_BUILD_TESTS)
    juce_add_console_app(rk_shared_tests PRODUCT_NAME "Raumklang Shared Tests")
    target_sources(rk_shared_tests PRIVATE tests/SharedTests.cpp)
    target_link_libraries(rk_shared_tests PRIVATE rk_shared rk_juce_defaults
        juce::juce_core juce::juce_data_structures juce::juce_events juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra
        juce::juce_recommended_config_flags)
    target_compile_definitions(rk_shared_tests PRIVATE JUCE_UNIT_TESTS=1
        RK_TEST_SOFA="${mysofa_SOURCE_DIR}/share/MIT_KEMAR_normal_pinna.sofa")
    add_test(NAME rk_shared_tests COMMAND rk_shared_tests)
endif()

# JUCE's recommended warnings plus a few that are noisy for DSP/UI code.
add_library(rk_warnings INTERFACE)
target_link_libraries(rk_warnings INTERFACE juce::juce_recommended_warning_flags)
if(NOT MSVC)
    target_compile_options(rk_warnings INTERFACE -Wno-float-equal -Wno-switch-enum)
endif()
