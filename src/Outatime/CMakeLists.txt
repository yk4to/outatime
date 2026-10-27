add_executable(Outatime WIN32
    main.cpp
    alt_tap_hook.cpp
    ime.cpp
    Outatime.manifest
)

target_compile_definitions(Outatime PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(Outatime PRIVATE /W4 /permissive- /utf-8)

target_link_libraries(Outatime PRIVATE user32 shell32 imm32)

# Link the CRT statically so the zip runs without the VC++ redistributable.
set_property(TARGET Outatime PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
