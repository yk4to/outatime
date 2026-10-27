#include "ime.hpp"

#include <windows.h>
#include <imm.h>

namespace outatime::ime {
namespace {

// Don't let a hung app stall us.
constexpr UINT kTimeoutMs = 100;

// WM_IME_CONTROL commands, not exposed by the public imm.h.
constexpr WPARAM IMC_GETOPENSTATUS = 0x0005;
constexpr WPARAM IMC_SETOPENSTATUS = 0x0006;

HWND FocusedWindow() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return nullptr;
    }
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info) && info.hwndFocus) {
        return info.hwndFocus;
    }
    return foreground;
}

bool SendImeControl(HWND imeWindow, WPARAM command, LPARAM value, DWORD_PTR* result) {
    return SendMessageTimeoutW(imeWindow, WM_IME_CONTROL, command, value, SMTO_ABORTIFHUNG,
                               kTimeoutMs, result) != 0;
}

}  // namespace

void Apply(Action action) {
    HWND target = FocusedWindow();
    if (!target) {
        return;
    }
    HWND imeWindow = ImmGetDefaultIMEWnd(target);
    if (!imeWindow) {
        return;
    }

    bool open = false;
    switch (action) {
    case Action::On:
        open = true;
        break;
    case Action::Off:
        open = false;
        break;
    case Action::Toggle: {
        DWORD_PTR current = 0;
        if (!SendImeControl(imeWindow, IMC_GETOPENSTATUS, 0, &current)) {
            return;
        }
        open = current == 0;
        break;
    }
    }
    SendImeControl(imeWindow, IMC_SETOPENSTATUS, open ? 1 : 0, nullptr);
}

}  // namespace outatime::ime
