include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

if(RK_BUILD_TESTS)
    add_executable(rk_tests
        main.cpp
        alloc_guard.cpp
        test_layout.cpp
        test_vbap.cpp
        test_dbap.cpp
        test_ambisonics.cpp
        test_reverb.cpp
        test_binaural.cpp
        test_rt_swap.cpp
        test_engine.cpp
        test_stress.cpp
        test_speaker_processor.cpp
        test_bridge.cpp)
    target_link_libraries(rk_tests PRIVATE rk::core doctest::doctest)
    if(NOT MSVC)
        target_compile_options(rk_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME rk_tests COMMAND rk_tests)
endif()

if(RK_FUZZ)
    add_executable(rk_fuzz_engine fuzz_engine.cpp)
    target_link_libraries(rk_fuzz_engine PRIVATE rk::core)
    target_compile_options(rk_fuzz_engine PRIVATE -fsanitize=fuzzer,address,undefined -g)
    target_link_options(rk_fuzz_engine PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
