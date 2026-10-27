#pragma once

namespace outatime::ime {

enum class Action {
    Toggle,
    On,
    Off,
};

// Changes the IME open state of the window that has keyboard focus.
void Apply(Action action);

}  // namespace outatime::ime
