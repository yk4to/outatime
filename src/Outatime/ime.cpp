#include "ime.hpp"

#include <windows.h>
#include <imm.h>

#include <cwchar>
#include <optional>

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

struct Target {
    HWND window;     // Focused window, for diagnostics
    HWND imeWindow;  // Its thread's default IME window
};

std::optional<Target> Resolve(HWND foreground) {
    if (!foreground) {
        diagnostics::LogFailure(L"no-foreground-window", nullptr);
        return std::nullopt;
    }
    HWND window = FocusedWindow(foreground);
    HWND imeWindow = ImmGetDefaultIMEWnd(window);
    if (!imeWindow) {
        diagnostics::LogFailure(L"no-ime-window", window);
        return std::nullopt;
    }
    return Target{window, imeWindow};
}

void SetAndVerify(const Target& target, bool open) {
    if (!SendImeControl(target.imeWindow, IMC_SETOPENSTATUS, open ? 1 : 0, nullptr, L"set-failed",
                        target.window)) {
        return;
    }

    // Read the state back to catch apps that accept the message but ignore it.
    for (int attempt = 1;; ++attempt) {
        DWORD_PTR actual = 0;
        if (!SendImeControl(target.imeWindow, IMC_GETOPENSTATUS, 0, &actual, L"verify-failed",
                            target.window)) {
            return;
        }
        if ((actual != 0) == open) {
            return;
        }
        if (attempt == kVerifyAttempts) {
            wchar_t detail[64];
            swprintf_s(detail, L"expected=%s actual=%s", StateName(open), StateName(actual != 0));
            diagnostics::LogFailure(L"state-mismatch", target.window, detail);
            return;
        }
        Sleep(kVerifyIntervalMs);
    }
}

}  // namespace

std::optional<bool> Apply(Action action, HWND foreground) {
    const auto target = Resolve(foreground);
    if (!target) {
        return std::nullopt;
    }

    // Read the current state even for On/Off so a double tap can restore it.
    // On/Off still set the state if this fails, and never skip setting because
    // the state already looks right: the reading may not match what the user sees.
    std::optional<bool> previous;
    if (DWORD_PTR current = 0; SendImeControl(target->imeWindow, IMC_GETOPENSTATUS, 0, &current,
                                              L"get-failed", target->window)) {
        previous = current != 0;
    }

    bool open = false;
    switch (action) {
    case Action::On:
        open = true;
        break;
    case Action::Off:
        open = false;
        break;
    case Action::Toggle:
        if (!previous) {
            return std::nullopt;
        }
        open = !*previous;
        break;
    }
    SetAndVerify(*target, open);
    return previous;
}

void SetOpen(HWND foreground, bool open) {
    if (const auto target = Resolve(foreground)) {
        SetAndVerify(*target, open);
    }
}

}  // namespace outatime::ime
