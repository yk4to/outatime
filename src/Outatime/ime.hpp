#pragma once

#include <windows.h>

namespace outatime::ime {

enum class Action {
    Toggle,
    On,
    Off,
};

// Changes the IME open state of the focused window in `foreground`'s thread,
// then reads it back. Failures are written to the diagnostics log.
void Apply(Action action, HWND foreground);

}  // namespace outatime::ime
