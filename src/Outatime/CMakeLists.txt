include(FetchContent)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SYSTEM
)
FetchContent_MakeAvailable(json)

# Logic without Win32 dependencies, shared with the unit tests.
add_library(outatime_core STATIC
    tap_detector.cpp
    settings.cpp
)
target_include_directories(outatime_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(outatime_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(outatime_core PRIVATE ${OUTATIME_COMPILE_OPTIONS})

add_executable(Outatime WIN32
    main.cpp
    app.cpp
    full_screen.cpp
    settings_app.cpp
    settings_watcher.cpp
    tray_icon.cpp
    win_util.cpp
    tap_hook.cpp
    trace.cpp
    ime.cpp
    diagnostics.cpp
    settings_file.cpp
    Outatime.rc
    Outatime.manifest
)

target_compile_definitions(Outatime PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(Outatime PRIVATE ${OUTATIME_COMPILE_OPTIONS})

target_link_libraries(Outatime PRIVATE
    outatime_core
    user32 shell32 imm32 ole32 advapi32 comctl32
)

# Same layout as the zip: Outatime.exe + settings\Outatime.Settings.exe
set_property(TARGET Outatime PROPERTY
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/bin/$<CONFIG>")
