#pragma once

#include <windows.h>

#include <optional>

namespace outatime::ime {

enum class Action {
    Toggle,
    On,
    Off,
};

// Changes the IME open state of the focused window in `foreground`'s thread,
// then reads it back. Returns the state from before the change, so it can be
// restored, or nullopt if it could not be read. Failures are written to the
// diagnostics log.
std::optional<bool> Apply(Action action, HWND foreground);

// Sets the IME open state of the focused window in `foreground`'s thread.
void SetOpen(HWND foreground, bool open);

}  // namespace outatime::ime
