#include "settings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace outatime {
namespace {

std::optional<Mode> ParseMode(const std::string& value) {
    if (value == "toggle") {
        return Mode::Toggle;
    }
    if (value == "leftOffRightOn") {
        return Mode::LeftOffRightOn;
    }
    if (value == "disabled") {
        return Mode::Disabled;
    }
    return std::nullopt;
}

std::optional<FullScreenPause> ParseFullScreenPause(const std::string& value) {
    if (value == "off") {
        return FullScreenPause::Off;
    }
    if (value == "games") {
        return FullScreenPause::Games;
    }
    if (value == "all") {
        return FullScreenPause::All;
    }
    return std::nullopt;
}

}  // namespace

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
    const auto json = nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (!json.is_object()) {
        return std::nullopt;
    }

    Settings settings;
    if (const auto it = json.find("mode"); it != json.end() && it->is_string()) {
        settings.mode = ParseMode(it->get<std::string>()).value_or(settings.mode);
    }
    if (const auto it = json.find("pauseInFullScreen"); it != json.end() && it->is_string()) {
        settings.pauseInFullScreen =
            ParseFullScreenPause(it->get<std::string>()).value_or(settings.pauseInFullScreen);
    }
    return settings;
}

}  // namespace outatime
