include(FetchContent)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SYSTEM
)
FetchContent_MakeAvailable(json)

add_executable(Outatime WIN32
    main.cpp
    alt_tap_hook.cpp
    ime.cpp
    settings.cpp
    Outatime.manifest
)

target_compile_definitions(Outatime PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(Outatime PRIVATE /W4 /permissive- /utf-8)

target_link_libraries(Outatime PRIVATE user32 shell32 imm32 ole32 advapi32 nlohmann_json::nlohmann_json)

# Same layout as the zip: Outatime.exe + settings\Outatime.Settings.exe
set_property(TARGET Outatime PROPERTY
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/bin/$<CONFIG>")

# Link the CRT statically so the zip runs without the VC++ redistributable.
set_property(TARGET Outatime PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
