#pragma once

#include <filesystem>
#include <optional>

namespace outatime {

enum class Mode {
    Toggle,          // Left Alt toggles the IME
    LeftOffRightOn,  // Left Alt turns it off, Right Alt turns it on
    Disabled,
};

struct Settings {
    Mode mode = Mode::Toggle;
};

// %APPDATA%\Outatime
std::filesystem::path SettingsDirectory();

// Returns defaults if settings.json does not exist, or nullopt if it exists
// but cannot be read or parsed. Unknown or invalid values fall back to defaults.
std::optional<Settings> LoadSettings();

}  // namespace outatime
