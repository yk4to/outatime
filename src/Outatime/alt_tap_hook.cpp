#include "alt_tap_hook.hpp"

#include "alt_tap_detector.hpp"
#include "trace.hpp"

namespace outatime {
namespace {

// Unassigned virtual key, injected between Alt down and up so the focused app
// does not treat the Alt release as a menu bar activation.
constexpr WORD kMaskKey = 0xE8;

// Asks the hook thread to remove the mouse hook once no Alt key is held.
constexpr UINT WM_UPDATE_MOUSE_HOOK = WM_APP + 1;

class PhysicalInputState final : public InputState {
public:
    bool IsDown(std::uint32_t vk) const override {
        return (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
    }
};

HWND g_target = nullptr;
UINT g_tapMessage = 0;
UINT g_doubleTapMessage = 0;

HANDLE g_thread = nullptr;
DWORD g_threadId = 0;

const PhysicalInputState g_input;
// Its setters are called from the main thread; everything else from the hook thread.
AltTapDetector g_detector(g_input);

// Touched only on the hook thread.
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);

#ifndef NDEBUG
// Traces Alt events, and only that some other key moved shortly after one (never
// which key), so the timing around a tap can be read from trace.log.
void TraceKey(const KeyEvent& event, Decision decision) {
    static DWORD lastAltTime = 0;  // Hook thread only
    const bool alt = event.vk == VK_LMENU || event.vk == VK_RMENU;
    if (alt) {
        lastAltTime = event.time;
    } else if (event.time - lastAltTime > 1000) {
        return;
    }
    static constexpr const wchar_t* kDecisions[] = {L"pass", L"TAP", L"DOUBLE-TAP"};
    OUTATIME_TRACE(L"hook  %-10s %-4s%s time=%lu -> %s",
                   alt              ? (event.vk == VK_LMENU ? L"LAlt" : L"RAlt")
                   : event.injected ? L"injected"
                                    : L"key",
                   event.down ? L"down" : L"up", event.injected ? L" (injected)" : L"", event.time,
                   kDecisions[static_cast<int>(decision)]);
}
#endif

// The mouse hook is only needed while an Alt key is held, so it is installed on
// demand. Call after giving the detector an event, with AltHeld() from before.
void UpdateMouseHook(bool wasAltHeld) {
    const bool altHeld = g_detector.AltHeld();
    if (altHeld && !g_mouseHook) {
        // Install right here rather than via the message loop, so a click right after
        // the Alt press is not missed.
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    } else if (!altHeld && wasAltHeld) {
        // Removal can wait; MouseProc calls this, and it should not unhook itself.
        PostThreadMessageW(g_threadId, WM_UPDATE_MOUSE_HOOK, 0, 0);
    }
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
    const KeyEvent event{
        .vk = kb.vkCode,
        .scanCode = kb.scanCode,
        .down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN,
        .injected = (kb.flags & LLKHF_INJECTED) != 0,
        .time = kb.time,
    };
    const auto key = static_cast<WPARAM>(kb.vkCode == VK_LMENU ? AltKey::Left : AltKey::Right);

    const bool wasAltHeld = g_detector.AltHeld();
    const Decision decision = g_detector.OnKey(event);
    UpdateMouseHook(wasAltHeld);
#ifndef NDEBUG
    TraceKey(event, decision);
#endif

    switch (decision) {
    case Decision::Tap:
        // Swallow the real key-up and replay it after the mask key. If that fails,
        // fall back to a plain Alt press.
        if (ReplayMaskedAltUp(kb.vkCode)) {
            OUTATIME_TRACE(L"hook  replayed masked key-up, posting tap");
            PostMessageW(g_target, g_tapMessage, key,
                         reinterpret_cast<LPARAM>(GetForegroundWindow()));
            return 1;
        }
        OUTATIME_TRACE(L"hook  replay FAILED, letting the key-up through");
        g_detector.OnTapNotReplayed();
        break;
    case Decision::DoubleTap:
        PostMessageW(g_target, g_doubleTapMessage, key, 0);
        break;
    case Decision::PassThrough:
        break;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam != WM_MOUSEMOVE) {
        const bool wasAltHeld = g_detector.AltHeld();
        g_detector.OnMouseInput();
        if (wasAltHeld) {
            OUTATIME_TRACE(L"hook  mouse input while Alt held -> combination");
        }
        UpdateMouseHook(wasAltHeld);
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
        if (msg.message == WM_UPDATE_MOUSE_HOOK && !g_detector.AltHeld() && g_mouseHook) {
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

bool StartAltTapHook(HWND target, UINT tapMessage, UINT doubleTapMessage) {
    g_target = target;
    g_tapMessage = tapMessage;
    g_doubleTapMessage = doubleTapMessage;

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

void SetAltTapKeys(bool left, bool right) { g_detector.SetWatchedKeys(left, right); }

void SetDoubleTapMs(DWORD ms) { g_detector.SetDoubleTapMs(ms); }

}  // namespace outatime
