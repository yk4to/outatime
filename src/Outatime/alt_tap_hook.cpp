#include "alt_tap_hook.hpp"

#include <atomic>
#include <bitset>

namespace outatime {
namespace {

// Unassigned virtual key, injected between Alt down and up so the focused app
// does not treat the Alt release as a menu bar activation.
constexpr WORD kMaskKey = 0xE8;

// AltGr layouts report Right Alt as a fake Left Ctrl with this scan code plus Right Alt.
constexpr DWORD kAltGrFakeCtrlScanCode = 0x21D;

// Asks the hook thread to install or remove the mouse hook to match g_candidate.
constexpr UINT WM_UPDATE_MOUSE_HOOK = WM_APP + 1;

HWND g_target = nullptr;
UINT g_message = 0;
std::atomic<bool> g_leftEnabled = false;
std::atomic<bool> g_rightEnabled = false;

HANDLE g_thread = nullptr;
DWORD g_threadId = 0;

// Everything below is touched only on the hook thread.
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;
std::bitset<256> g_pressed;
DWORD g_candidate = 0;  // VK of the Alt key that may become a tap, or 0.

bool IsWatched(DWORD vk) {
    return (vk == VK_LMENU && g_leftEnabled) || (vk == VK_RMENU && g_rightEnabled);
}

void SetCandidate(DWORD vk) {
    if (g_candidate == vk) {
        return;
    }
    const bool hadCandidate = g_candidate != 0;
    g_candidate = vk;
    // The mouse hook is only needed while an Alt key is held, so install it on demand.
    if (hadCandidate != (vk != 0)) {
        PostThreadMessageW(g_threadId, WM_UPDATE_MOUSE_HOOK, 0, 0);
    }
}

// Key-ups can be missed (e.g. released on the secure desktop), so drop keys
// that are no longer physically held before deciding nothing else is pressed.
bool NoOtherKeysPressed() {
    for (size_t vk = 0; vk < g_pressed.size(); ++vk) {
        if (g_pressed[vk] && !(GetAsyncKeyState(static_cast<int>(vk)) & 0x8000)) {
            g_pressed[vk] = false;
        }
    }
    return g_pressed.none();
}

void InjectMaskedAltUp(DWORD vk) {
    INPUT inputs[3]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = kMaskKey;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = static_cast<WORD>(vk);
    inputs[2].ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP | (vk == VK_RMENU ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const auto& kb = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    // Ignore our own injected keys (and other tools'), and the AltGr fake Ctrl.
    if ((kb.flags & LLKHF_INJECTED) || kb.vkCode >= g_pressed.size() ||
        (kb.vkCode == VK_LCONTROL && kb.scanCode == kAltGrFakeCtrlScanCode)) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const DWORD vk = kb.vkCode;
    const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    if (down) {
        const bool repeat = g_pressed[vk];
        if (!repeat) {
            SetCandidate(IsWatched(vk) && NoOtherKeysPressed() ? vk : 0);
        }
        g_pressed[vk] = true;
    } else {
        g_pressed[vk] = false;
        if (vk == g_candidate) {
            SetCandidate(0);
            // Swallow the real key-up and replay it after the mask key.
            InjectMaskedAltUp(vk);
            PostMessageW(g_target, g_message,
                         static_cast<WPARAM>(vk == VK_LMENU ? AltKey::Left : AltKey::Right), 0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    // Any click or wheel turns the Alt press into a combination (e.g. Alt+click).
    if (code == HC_ACTION && wParam != WM_MOUSEMOVE) {
        SetCandidate(0);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI HookThread(LPVOID readyEvent) {
    // Create the message queue before signaling that thread messages can be posted.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance, 0);
    SetEvent(static_cast<HANDLE>(readyEvent));
    if (!g_keyboardHook) {
        return 1;
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_UPDATE_MOUSE_HOOK) {
            if (g_candidate != 0 && !g_mouseHook) {
                g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, instance, 0);
            } else if (g_candidate == 0 && g_mouseHook) {
                UnhookWindowsHookEx(g_mouseHook);
                g_mouseHook = nullptr;
            }
        }
    }

    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    UnhookWindowsHookEx(g_keyboardHook);
    g_keyboardHook = nullptr;
    return 0;
}

}  // namespace

bool StartAltTapHook(HWND target, UINT message) {
    g_target = target;
    g_message = message;

    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, HookThread, ready, 0, &g_threadId);
    if (!g_thread) {
        CloseHandle(ready);
        return false;
    }
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);

    // The thread exits right away if the hook could not be installed.
    if (WaitForSingleObject(g_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_thread);
        g_thread = nullptr;
        return false;
    }
    return true;
}

void StopAltTapHook() {
    if (!g_thread) {
        return;
    }
    PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    g_thread = nullptr;
}

void SetAltTapKeys(bool left, bool right) {
    g_leftEnabled = left;
    g_rightEnabled = right;
}

}  // namespace outatime
