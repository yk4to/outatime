#pragma once

#include <windows.h>

#include <optional>

namespace outatime::ime {

enum class Action {
    Toggle,
    On,
    Off,
};

struct ApplyResult {
    std::optional<bool> previous;   // Open state before the change, so it can be restored
    std::optional<bool> requested;  // Open state that was set, if any
};

// Changes the IME open state of the focused window in `foreground`'s thread,
// then reads it back. Failures are written to the diagnostics log.
ApplyResult Apply(Action action, HWND foreground);

// Sets the IME open state of the focused window in `foreground`'s thread.
void SetOpen(HWND foreground, bool open);

// Reads the IME open state of the focused window in `foreground`'s thread.
std::optional<bool> GetOpen(HWND foreground);

// Logs "state-reverted" if the IME open state is no longer `expected`, which
// catches apps that accept a change and undo it shortly after.
void Recheck(HWND foreground, bool expected);

}  // namespace outatime::ime
