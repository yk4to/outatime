#pragma once

#include <filesystem>
#include <optional>

#include "settings.hpp"

namespace outatime {

// %APPDATA%\Outatime, or in the MSIX package, the package's LocalState folder.
std::filesystem::path SettingsDirectory();

// Returns defaults if settings.json does not exist, or nullopt if it exists
// but cannot be read or parsed (see ParseSettings).
std::optional<Settings> LoadSettings();

}  // namespace outatime
