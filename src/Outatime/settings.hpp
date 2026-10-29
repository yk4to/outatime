#pragma once

#include <optional>
#include <string_view>

namespace outatime {

enum class Mode {
    Toggle,          // Left Alt toggles the IME
    LeftOffRightOn,  // Left Alt turns it off, Right Alt turns it on
    Disabled,
};

// When to step aside so full-screen apps (mostly games) get the Alt key as is.
enum class FullScreenPause {
    Off,
    Games,  // Exclusive full-screen Direct3D apps (QUNS_RUNNING_D3D_FULL_SCREEN)
    All,    // Also whatever else the shell reports as full screen (QUNS_BUSY)
};

struct Settings {
    Mode mode = Mode::Toggle;
    FullScreenPause pauseInFullScreen = FullScreenPause::Games;
    // Pressing the same Alt key again within doubleTapMs after a tap undoes the
    // tap and gives a plain Alt (e.g. for the menu bar).
    bool doubleTap = true;
    unsigned doubleTapMs = 200;
    // Also watch Alt keys sent by other programs, such as remote desktop tools
    // (e.g. Parsec). Off by default, since a script that sends a lone Alt (e.g.
    // an AutoHotkey remap) would then switch the IME too.
    bool acceptInjectedKeys = false;
};

// Parses the contents of settings.json. Returns nullopt if it is not a JSON
// object; unknown keys are ignored and unknown or invalid values fall back to
// their defaults, so a file from a newer or older version still loads.
std::optional<Settings> ParseSettings(std::string_view json);

}  // namespace outatime
