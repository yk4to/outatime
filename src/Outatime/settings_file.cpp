#include "settings_file.hpp"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <iterator>
#include <string>

#include "win_util.hpp"

namespace outatime {

// Must match SettingsStore.cs in the settings app.
std::filesystem::path SettingsDirectory() {
    // In the package, new files under %APPDATA% go to a private copy that editors
    // outside the package can't see. ApplicationData.LocalFolder, which the
    // settings app uses, is this folder.
    const std::wstring family = PackageFamilyName();
    PWSTR appData = nullptr;
    std::filesystem::path path;
    if (family.empty()) {
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr,
                                           &appData))) {
            path = std::filesystem::path(appData) / L"Outatime";
        }
    } else if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr,
                                              &appData))) {
        path = std::filesystem::path(appData) / L"Packages" / family / L"LocalState";
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
