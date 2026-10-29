#pragma once

#include <windows.h>

#include "tap_key.hpp"

namespace outatime {

// Watches for Alt and Windows keys tapped on their own (see TapDetector for
// the rules) using low-level hooks on a dedicated thread.
//
// A detected tap is swallowed so it does not open the menu bar or the Start menu,
// and `tapMessage` is posted to `target` with the TapKey as wParam and the
// foreground window at the time of the tap as lParam, so focus changes before it
// is handled don't matter.
//
// If the same key is pressed again within the double-tap time, that press is let
// through as a plain key and `doubleTapMessage` is posted with the TapKey as
// wParam, so the target can undo what the first tap did.
bool StartTapHook(HWND target, UINT tapMessage, UINT doubleTapMessage);
void StopTapHook();

// Which keys to watch. Taps on the others pass through untouched.
void SetTapKeys(TapKeySet keys);

// Time from a tap's release to the next press of the same key that counts as a
// double tap. 0 turns double taps off.
void SetDoubleTapMs(DWORD ms);

// Whether keys sent by other programs (e.g. remote desktop tools) are watched
// too. Our own replayed keys are always ignored.
void SetAcceptInjectedKeys(bool accept);

}  // namespace outatime
