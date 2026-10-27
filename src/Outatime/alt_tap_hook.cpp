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

// Asks the hook thread to remove the mouse hook once no Alt key is held.
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

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);

// The mouse hook is only needed while an Alt key is held, so it is installed on demand.
void SetCandidate(DWORD vk) {
    if (g_candidate == vk) {
        return;
    }
    const bool hadCandidate = g_candidate != 0;
    g_candidate = vk;
    if (vk != 0 && !g_mouseHook) {
        // Install right here rather than via the message loop, so a click right after
        // the Alt press is not missed.
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    } else if (vk == 0 && hadCandidate) {
        // Removal can wait; MouseProc calls this, and it should not unhook itself.
        PostThreadMessageW(g_threadId, WM_UPDATE_MOUSE_HOOK, 0, 0);
    }
}

// Catches buttons held across the Alt press (e.g. Alt+drag started before Alt),
// which the mouse hook never sees go down.
bool AnyMouseButtonDown() {
    for (const int vk : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            return true;
        }
    }
    return false;
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

// Sends the mask key and then the Alt key-up. Returns false if the Alt key-up was
// not injected (e.g. blocked by UIPI); the caller must then let the real key-up
// through, or Alt would stay down.
bool ReplayMaskedAltUp(DWORD vk) {
    INPUT inputs[3]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = kMaskKey;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = static_cast<WORD>(vk);
    inputs[2].ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP | (vk == VK_RMENU ? KEYEVENTF_EXTENDEDKEY : 0);
    const UINT sent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
    if (sent == 1) {
        // Only the mask key-down got through; release it so it is not left held.
        SendInput(1, &inputs[1], sizeof(INPUT));
    }
    return sent == ARRAYSIZE(inputs);
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
            const bool alone = NoOtherKeysPressed() && !AnyMouseButtonDown();
            SetCandidate(IsWatched(vk) && alone ? vk : 0);
        }
        g_pressed[vk] = true;
    } else {
        g_pressed[vk] = false;
        if (vk == g_candidate) {
            SetCandidate(0);
            // Swallow the real key-up and replay it after the mask key. If that fails,
            // fall back to a plain Alt press.
            if (!AnyMouseButtonDown() && ReplayMaskedAltUp(vk)) {
                PostMessageW(g_target, g_message,
                             static_cast<WPARAM>(vk == VK_LMENU ? AltKey::Left : AltKey::Right), 0);
                return 1;
            }
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

// Filled in by the hook thread before it signals `ready`. Lives on the stack of
// StartAltTapHook, which waits for the signal, so it must not be touched afterwards.
struct StartupResult {
    HANDLE ready;
    bool hooked;
};

DWORD WINAPI HookThread(LPVOID param) {
    auto* startup = static_cast<StartupResult*>(param);

    // Create the message queue before signaling that thread messages can be posted.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance, 0);
    startup->hooked = g_keyboardHook != nullptr;
    SetEvent(startup->ready);
    if (!g_keyboardHook) {
        return 1;
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // A new Alt press may have arrived since the removal was requested.
        if (msg.message == WM_UPDATE_MOUSE_HOOK && g_candidate == 0 && g_mouseHook) {
            UnhookWindowsHookEx(g_mouseHook);
            g_mouseHook = nullptr;
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

    StartupResult startup{CreateEventW(nullptr, TRUE, FALSE, nullptr), false};
    if (!startup.ready) {
        return false;
    }
    g_thread = CreateThread(nullptr, 0, HookThread, &startup, 0, &g_threadId);
    if (!g_thread) {
        CloseHandle(startup.ready);
        return false;
    }

    // Also wake up if the thread dies before signaling, so we never wait forever.
    const HANDLE handles[] = {startup.ready, g_thread};
    const DWORD signaled = WaitForMultipleObjects(ARRAYSIZE(handles), handles, FALSE, INFINITE);
    CloseHandle(startup.ready);

    if (signaled != WAIT_OBJECT_0 || !startup.hooked) {
        // The thread returns right after a failed install; wait so it is fully gone.
        WaitForSingleObject(g_thread, INFINITE);
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
