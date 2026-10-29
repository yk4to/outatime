#include "settings.hpp"

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

std::optional<Settings> ParseSettings(std::string_view text) {
    const auto json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
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
    if (const auto it = json.find("doubleTap"); it != json.end() && it->is_boolean()) {
        settings.doubleTap = it->get<bool>();
    }
    // Values past a second would turn deliberate repeated taps into plain Alt presses.
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
