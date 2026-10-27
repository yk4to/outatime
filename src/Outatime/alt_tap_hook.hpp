#pragma once

#include <windows.h>

namespace outatime {

enum class AltKey : WPARAM {
    Left,
    Right,
};

// Watches for Alt keys tapped on their own (pressed and released with no other
// key or mouse button in between) using low-level hooks on a dedicated thread.
// A detected tap is swallowed so it does not open the menu bar, and `message`
// is posted to `target` with the AltKey as wParam and the foreground window at
// the time of the tap as lParam, so focus changes before it is handled don't matter.
bool StartAltTapHook(HWND target, UINT message);
void StopAltTapHook();

// Which Alt keys to watch. Taps on other Alt keys pass through untouched.
void SetAltTapKeys(bool left, bool right);

}  // namespace outatime
