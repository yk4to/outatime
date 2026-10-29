#pragma once

#include <windows.h>

namespace outatime {

enum class AltKey : WPARAM {
    Left,
    Right,
};

// Watches for Alt keys tapped on their own (see AltTapDetector for the rules)
// using low-level hooks on a dedicated thread.
//
// A detected tap is swallowed so it does not open the menu bar, and `tapMessage`
// is posted to `target` with the AltKey as wParam and the foreground window at
// the time of the tap as lParam, so focus changes before it is handled don't matter.
//
// If the same Alt key is pressed again within the double-tap time, that press is
// let through as a plain Alt and `doubleTapMessage` is posted with the AltKey as
// wParam, so the target can undo what the first tap did.
bool StartAltTapHook(HWND target, UINT tapMessage, UINT doubleTapMessage);
void StopAltTapHook();

// Which Alt keys to watch. Taps on other Alt keys pass through untouched.
void SetAltTapKeys(bool left, bool right);

// Time from a tap's release to the next press of the same key that counts as a
// double tap. 0 turns double taps off.
void SetDoubleTapMs(DWORD ms);

// Whether keys sent by other programs (e.g. remote desktop tools) are watched
// too. Our own replayed keys are always ignored.
void SetAcceptInjectedKeys(bool accept);

}  // namespace outatime
