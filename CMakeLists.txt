cmake_minimum_required(VERSION 3.22)

project(Raumklang VERSION 0.1.0 LANGUAGES C CXX)

# Windows: link the C++ runtime statically, so the app and plugins run without
# installing the Visual C++ Redistributable first.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(RK_BUILD_TESTS   "Build the core unit and stress tests" ON)
option(RK_BUILD_APP     "Build the standalone app (needs JUCE)" ON)
option(RK_BUILD_PLUGINS "Build the DAW plugins (needs JUCE)" ON)
option(RK_FUZZ          "Build libFuzzer targets (clang only)" OFF)
option(RK_WERROR        "Treat warnings as errors" OFF)
set(RK_SANITIZE "" CACHE STRING "Sanitizers for core and tests, e.g. address,undefined or thread")
set(RK_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading it")

if(RK_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${RK_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${RK_SANITIZE})
endif()

add_subdirectory(core)

if(RK_BUILD_TESTS OR RK_FUZZ)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RK_BUILD_APP OR RK_BUILD_PLUGINS)
    include(FetchContent)
    if(RK_JUCE_DIR)
        add_subdirectory(${RK_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 9.0.5
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(juce)
    endif()
    add_subdirectory(shared)
    if(RK_BUILD_APP)
        add_subdirectory(app)
    endif()
    if(RK_BUILD_PLUGINS)
        add_subdirectory(plugins)
    endif()
endif()
