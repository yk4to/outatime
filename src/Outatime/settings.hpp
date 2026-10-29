#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "tap_key.hpp"

namespace outatime {

// What tapping a key on its own does.
enum class KeyAction {
    None,
    Toggle,  // Turns the IME on if it is off, and off if it is on
    On,
    Off,
};

// When to step aside so full-screen apps (mostly games) get the keys as is.
enum class FullScreenPause {
    Off,
    Games,  // Exclusive full-screen Direct3D apps (QUNS_RUNNING_D3D_FULL_SCREEN)
    All,    // Also whatever else the shell reports as full screen (QUNS_BUSY)
};

struct Settings {
    // Indexed by TapKey. By default only Left Alt does something: it toggles.
    std::array<KeyAction, kTapKeyCount> keys = {KeyAction::Toggle, KeyAction::None, KeyAction::None,
                                                KeyAction::None};
    FullScreenPause pauseInFullScreen = FullScreenPause::Games;
    // Pressing the same key again within doubleTapMs after a tap undoes the tap
    // and gives a plain key press (e.g. for the menu bar or the Start menu).
    bool doubleTap = true;
    unsigned doubleTapMs = 200;
    // Also watch keys sent by other programs, such as remote desktop tools
    // (e.g. Parsec). Off by default, since a script that sends a lone Alt (e.g.
    // an AutoHotkey remap) would then switch the IME too.
    bool acceptInjectedKeys = false;

    KeyAction ActionFor(TapKey key) const { return keys[Index(key)]; }
};

// Parses the contents of settings.json. Returns nullopt if it is not a JSON
// object; unknown keys are ignored and unknown or invalid values fall back to
// their defaults, so a file from a newer or older version still loads.
std::optional<Settings> ParseSettings(std::string_view json);

}  // namespace outatime
