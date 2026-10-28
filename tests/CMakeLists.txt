include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(outatime_tests
    alt_tap_detector_test.cpp
    settings_test.cpp
)
target_compile_options(outatime_tests PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(outatime_tests PRIVATE outatime_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(outatime_tests)
