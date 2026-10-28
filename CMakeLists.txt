cmake_minimum_required(VERSION 3.25)

project(Outatime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the CRT statically so the zip runs without the VC++ redistributable.
# Set for every target so the tests and GoogleTest link the same CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(OUTATIME_BUILD_TESTS "Build the unit tests" ON)

add_subdirectory(src/Outatime)

if(OUTATIME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
