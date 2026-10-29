#include "settings.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace outatime {
namespace {

// Names in settings.json, indexed by TapKey.
constexpr const char* kKeyNames[kTapKeyCount] = {"leftAlt", "rightAlt", "leftWin", "rightWin"};

std::optional<KeyAction> ParseKeyAction(const std::string& value) {
    if (value == "none") {
        return KeyAction::None;
    }
    if (value == "toggle") {
        return KeyAction::Toggle;
    }
    if (value == "on") {
        return KeyAction::On;
    }
    if (value == "off") {
        return KeyAction::Off;
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

std::optional<Settings> ParseSettings(std::string_view text) {
    const auto json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!json.is_object()) {
        return std::nullopt;
    }

    Settings settings;
    if (const auto keys = json.find("keys"); keys != json.end() && keys->is_object()) {
        // Keys that are missing or have an unknown action keep their defaults.
        for (std::size_t i = 0; i < kTapKeyCount; ++i) {
            if (const auto it = keys->find(kKeyNames[i]); it != keys->end() && it->is_string()) {
                settings.keys[i] =
                    ParseKeyAction(it->get<std::string>()).value_or(settings.keys[i]);
            }
        }
    }
    if (const auto it = json.find("pauseInFullScreen"); it != json.end() && it->is_string()) {
        settings.pauseInFullScreen =
            ParseFullScreenPause(it->get<std::string>()).value_or(settings.pauseInFullScreen);
    }
    if (const auto it = json.find("doubleTap"); it != json.end() && it->is_boolean()) {
        settings.doubleTap = it->get<bool>();
    }
    // Values past a second would turn deliberate repeated taps into plain key presses.
    if (const auto it = json.find("doubleTapMs");
        it != json.end() && it->is_number_integer() && *it >= 0 && *it <= 1000) {
        settings.doubleTapMs = it->get<unsigned>();
    }
    if (const auto it = json.find("acceptInjectedKeys"); it != json.end() && it->is_boolean()) {
        settings.acceptInjectedKeys = it->get<bool>();
    }
    return settings;
}

}  // namespace outatime
