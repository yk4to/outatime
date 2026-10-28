#pragma once

#include <filesystem>
#include <optional>

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
};

// %APPDATA%\Outatime
std::filesystem::path SettingsDirectory();

// Returns defaults if settings.json does not exist, or nullopt if it exists
// but cannot be read or parsed. Unknown or invalid values fall back to defaults.
std::optional<Settings> LoadSettings();

}  // namespace outatime
