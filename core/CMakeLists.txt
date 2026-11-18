add_library(rk_core STATIC
    src/math.cpp
    src/layout.cpp
    src/panners.cpp
    src/reverb.cpp
    src/binaural.cpp
    src/engine.cpp
    src/speaker_processor.cpp
    src/bridge.cpp
    src/calibration.cpp)
add_library(rk::core ALIAS rk_core)

target_include_directories(rk_core PUBLIC include)
target_compile_features(rk_core PUBLIC cxx_std_20)
set_target_properties(rk_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(rk_core PRIVATE /W4 /permissive- $<$<BOOL:${RK_WERROR}>:/WX>)
else()
    target_compile_options(rk_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow
        $<$<BOOL:${RK_WERROR}>:-Werror>)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(rk_core PUBLIC rt) # shm_open on older glibc
endif()
