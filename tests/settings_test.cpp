#include "settings.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>

namespace outatime {
namespace {

using enum KeyAction;
using Keys = std::array<KeyAction, kTapKeyCount>;

// Parses `json`, which must be a valid settings object.
Settings Parse(const std::string& json) {
    const std::optional<Settings> settings = ParseSettings(json);
    EXPECT_TRUE(settings.has_value()) << json;
    return settings.value_or(Settings{});
}

TEST(SettingsTest, EmptyObjectGivesDefaults) {
    const Settings settings = Parse("{}");
    EXPECT_EQ(settings.keys, (Keys{Toggle, None, None, None}));
    EXPECT_EQ(settings.pauseInFullScreen, FullScreenPause::Games);
    EXPECT_TRUE(settings.doubleTap);
    EXPECT_EQ(settings.doubleTapMs, 200u);
    EXPECT_FALSE(settings.acceptInjectedKeys);
}

TEST(SettingsTest, ReadsEveryKey) {
    const Settings settings = Parse(R"({
        "keys": {"leftAlt": "off", "rightAlt": "on", "leftWin": "none", "rightWin": "toggle"},
        "pauseInFullScreen": "all",
        "doubleTap": false,
        "doubleTapMs": 350,
        "acceptInjectedKeys": true
    })");
    EXPECT_EQ(settings.keys, (Keys{Off, On, None, Toggle}));
    EXPECT_EQ(settings.ActionFor(TapKey::RightWin), Toggle);
    EXPECT_EQ(settings.pauseInFullScreen, FullScreenPause::All);
    EXPECT_FALSE(settings.doubleTap);
    EXPECT_EQ(settings.doubleTapMs, 350u);
    EXPECT_TRUE(settings.acceptInjectedKeys);
}

TEST(SettingsTest, ReadsEveryKeyAction) {
    EXPECT_EQ(Parse(R"({"keys": {"leftAlt": "none"}})").keys[0], None);
    EXPECT_EQ(Parse(R"({"keys": {"leftAlt": "toggle"}})").keys[0], Toggle);
    EXPECT_EQ(Parse(R"({"keys": {"leftAlt": "on"}})").keys[0], On);
    EXPECT_EQ(Parse(R"({"keys": {"leftAlt": "off"}})").keys[0], Off);
}

TEST(SettingsTest, MissingKeysKeepTheirDefaults) {
    EXPECT_EQ(Parse(R"({"keys": {"rightWin": "on"}})").keys, (Keys{Toggle, None, None, On}));
}

TEST(SettingsTest, ReadsEveryFullScreenPause) {
    EXPECT_EQ(Parse(R"({"pauseInFullScreen": "off"})").pauseInFullScreen, FullScreenPause::Off);
    EXPECT_EQ(Parse(R"({"pauseInFullScreen": "games"})").pauseInFullScreen, FullScreenPause::Games);
    EXPECT_EQ(Parse(R"({"pauseInFullScreen": "all"})").pauseInFullScreen, FullScreenPause::All);
}

TEST(SettingsTest, UnknownValuesFallBackToDefaults) {
    const Settings settings = Parse(R"({
        "keys": {"leftAlt": "sideways", "rightAlt": "on"},
        "pauseInFullScreen": "sometimes"
    })");
    EXPECT_EQ(settings.keys, (Keys{Toggle, On, None, None}));
    EXPECT_EQ(settings.pauseInFullScreen, FullScreenPause::Games);
}

TEST(SettingsTest, WrongTypesFallBackToDefaults) {
    const Settings settings = Parse(R"({
        "keys": {"leftAlt": false, "rightAlt": 1},
        "pauseInFullScreen": true,
        "doubleTap": "yes",
        "doubleTapMs": "300",
        "acceptInjectedKeys": 1
    })");
    EXPECT_EQ(settings.keys, (Keys{Toggle, None, None, None}));
    EXPECT_EQ(settings.pauseInFullScreen, FullScreenPause::Games);
    EXPECT_TRUE(settings.doubleTap);
    EXPECT_EQ(settings.doubleTapMs, 200u);
    EXPECT_FALSE(settings.acceptInjectedKeys);
    EXPECT_EQ(Parse(R"({"keys": "on"})").keys, (Keys{Toggle, None, None, None}));
}

TEST(SettingsTest, DoubleTapMsMustBeAnIntegerFromZeroToASecond) {
    EXPECT_EQ(Parse(R"({"doubleTapMs": 0})").doubleTapMs, 0u);
    EXPECT_EQ(Parse(R"({"doubleTapMs": 1000})").doubleTapMs, 1000u);
    EXPECT_EQ(Parse(R"({"doubleTapMs": 1001})").doubleTapMs, 200u);
    EXPECT_EQ(Parse(R"({"doubleTapMs": -1})").doubleTapMs, 200u);
    EXPECT_EQ(Parse(R"({"doubleTapMs": 150.5})").doubleTapMs, 200u);
}

TEST(SettingsTest, UnknownKeysAreIgnored) {
    // E.g. a file written by a newer version.
    const Settings settings =
        Parse(R"({"keys": {"leftAlt": "none", "capsLock": "on"}, "excludedApps": ["game.exe"]})");
    EXPECT_EQ(settings.keys, (Keys{None, None, None, None}));
}

TEST(SettingsTest, Utf8BomIsAccepted) {
    // Windows PowerShell's Set-Content -Encoding utf8 writes one.
    EXPECT_EQ(Parse("\xEF\xBB\xBF{\"keys\": {\"leftAlt\": \"none\"}}").keys[0], None);
}

TEST(SettingsTest, NotAnObjectIsRejected) {
    // The resident then keeps its current settings, e.g. while a file is being edited.
    EXPECT_FALSE(ParseSettings(""));
    EXPECT_FALSE(ParseSettings(R"({"keys": )"));
    EXPECT_FALSE(ParseSettings("[]"));
    EXPECT_FALSE(ParseSettings(R"("toggle")"));
    EXPECT_FALSE(ParseSettings("null"));
}

}  // namespace
}  // namespace outatime
