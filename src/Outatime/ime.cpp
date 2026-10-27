#include "ime.hpp"

#include <windows.h>
#include <imm.h>

#include <cwchar>

#include "diagnostics.hpp"

namespace outatime::ime {
namespace {

// Don't let a hung app stall us.
constexpr UINT kTimeoutMs = 100;

// The IME may report its new state a little later, so re-read a few times
// before calling it a failure. This runs on the main thread, not the hook
// thread, so waiting does not delay keyboard input.
constexpr int kVerifyAttempts = 4;
constexpr DWORD kVerifyIntervalMs = 20;

// WM_IME_CONTROL commands, not exposed by the public imm.h.
constexpr WPARAM IMC_GETOPENSTATUS = 0x0005;
constexpr WPARAM IMC_SETOPENSTATUS = 0x0006;

HWND FocusedWindow(HWND foreground) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info) && info.hwndFocus) {
        return info.hwndFocus;
    }
    return foreground;
}

// On failure, logs `event` with the error (ERROR_TIMEOUT for a hung app,
// ERROR_ACCESS_DENIED when UIPI blocks a more privileged window).
bool SendImeControl(HWND imeWindow, WPARAM command, LPARAM value, DWORD_PTR* result,
                    const wchar_t* event, HWND target) {
    if (SendMessageTimeoutW(imeWindow, WM_IME_CONTROL, command, value, SMTO_ABORTIFHUNG, kTimeoutMs,
                            result)) {
        return true;
    }
    wchar_t detail[32];
    swprintf_s(detail, L"error=%lu", GetLastError());
    diagnostics::LogFailure(event, target, detail);
    return false;
}

const wchar_t* StateName(bool open) { return open ? L"open" : L"closed"; }

}  // namespace

void Apply(Action action, HWND foreground) {
    if (!foreground) {
        diagnostics::LogFailure(L"no-foreground-window", nullptr);
        return;
    }
    HWND target = FocusedWindow(foreground);
    HWND imeWindow = ImmGetDefaultIMEWnd(target);
    if (!imeWindow) {
        diagnostics::LogFailure(L"no-ime-window", target);
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
        if (!SendImeControl(imeWindow, IMC_GETOPENSTATUS, 0, &current, L"get-failed", target)) {
            return;
        }
        open = current == 0;
        break;
    }
    }
    if (!SendImeControl(imeWindow, IMC_SETOPENSTATUS, open ? 1 : 0, nullptr, L"set-failed",
                        target)) {
        return;
    }

    // Read the state back to catch apps that accept the message but ignore it.
    for (int attempt = 1;; ++attempt) {
        DWORD_PTR actual = 0;
        if (!SendImeControl(imeWindow, IMC_GETOPENSTATUS, 0, &actual, L"verify-failed", target)) {
            return;
        }
        if ((actual != 0) == open) {
            return;
        }
        if (attempt == kVerifyAttempts) {
            wchar_t detail[64];
            swprintf_s(detail, L"expected=%s actual=%s", StateName(open), StateName(actual != 0));
            diagnostics::LogFailure(L"state-mismatch", target, detail);
            return;
        }
        Sleep(kVerifyIntervalMs);
    }
}

}  // namespace outatime::ime
