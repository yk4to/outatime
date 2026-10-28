#include "settings_file.hpp"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <iterator>
#include <string>

namespace outatime {

std::filesystem::path SettingsDirectory() {
    PWSTR appData = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &appData))) {
        path = std::filesystem::path(appData) / L"Outatime";
    }
    CoTaskMemFree(appData);
    return path;
}

std::optional<Settings> LoadSettings() {
    const auto path = SettingsDirectory() / L"settings.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Settings{};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return ParseSettings(text);
}

}  // namespace outatime
