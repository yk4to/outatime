#include "tap_hook.hpp"

#include "tap_detector.hpp"
#include "trace.hpp"

namespace outatime {
namespace {

// Unassigned virtual key, injected between a tap key's down and up so the focused
// app does not treat the Alt release as a menu bar activation, nor Windows the
// Windows key release as a request for the Start menu.
constexpr WORD kMaskKey = 0xE8;

// Tags the keys we send, so the hook can tell them from other programs' ("OUTA").
constexpr ULONG_PTR kOurExtraInfo = 0x4F555441;

// Asks the hook thread to remove the mouse hook once no tap key is held.
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
TapDetector g_detector(g_input);

// Touched only on the hook thread.
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;

LRESULT CALLBACK MouseProc(int code, WPARAM wParam, LPARAM lParam);

#ifndef NDEBUG
// Traces tap key events, and only that some other key moved shortly after one
// (never which key), so the timing around a tap can be read from trace.log.
void TraceKey(const KeyEvent& event, Decision decision) {
    static DWORD lastTapKeyTime = 0;  // Hook thread only
    const std::optional<TapKey> tapKey = TapKeyFromVirtualKey(event.vk);
    if (tapKey) {
        lastTapKeyTime = event.time;
    } else if (event.time - lastTapKeyTime > 1000) {
        return;
    }
    static constexpr const wchar_t* kTapKeys[] = {L"LAlt", L"RAlt", L"LWin", L"RWin"};
    static constexpr const wchar_t* kDecisions[] = {L"pass", L"TAP", L"DOUBLE-TAP"};
    static constexpr const wchar_t* kSources[] = {L"", L" (injected)", L" (ours)"};
    OUTATIME_TRACE(L"hook  %-10s %-4s%s time=%lu -> %s",
                   tapKey                                ? kTapKeys[Index(*tapKey)]
                   : event.source == KeySource::Physical ? L"key"
                                                         : L"injected",
                   event.down ? L"down" : L"up", kSources[static_cast<int>(event.source)],
                   event.time, kDecisions[static_cast<int>(decision)]);
}
#endif

// The mouse hook is only needed while a tap key is held, so it is installed on
// demand. Call after giving the detector an event, with TapKeyHeld() from before.
void UpdateMouseHook(bool wasHeld) {
    const bool held = g_detector.TapKeyHeld();
    if (held && !g_mouseHook) {
        // Install right here rather than via the message loop, so a click right after
        // the key press is not missed.
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    } else if (!held && wasHeld) {
        // Removal can wait; MouseProc calls this, and it should not unhook itself.
        PostThreadMessageW(g_threadId, WM_UPDATE_MOUSE_HOOK, 0, 0);
    }
}

// Sends the mask key and then the tap key's key-up. Returns false if the key-up
// was not injected (e.g. blocked by UIPI); the caller must then let the real
// key-up through, or the key would stay down.
bool ReplayMaskedKeyUp(DWORD vk) {
    // Right Alt and both Windows keys are extended keys.
    const bool extended = vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
    INPUT inputs[3]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = kMaskKey;
    inputs[0].ki.dwExtraInfo = kOurExtraInfo;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = static_cast<WORD>(vk);
    inputs[2].ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    inputs[2].ki.dwExtraInfo = kOurExtraInfo;
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
    const KeySource source = (kb.flags & LLKHF_INJECTED) == 0  ? KeySource::Physical
                             : kb.dwExtraInfo == kOurExtraInfo ? KeySource::Ours
                                                               : KeySource::Injected;
    const KeyEvent event{
        .vk = kb.vkCode,
        .scanCode = kb.scanCode,
        .down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN,
        .source = source,
        .time = kb.time,
    };

    const bool wasHeld = g_detector.TapKeyHeld();
    const Decision decision = g_detector.OnKey(event);
    UpdateMouseHook(wasHeld);
#ifndef NDEBUG
    TraceKey(event, decision);
#endif

    // Taps and double taps only come from watched keys, which are all tap keys.
    const auto key = static_cast<WPARAM>(TapKeyFromVirtualKey(kb.vkCode).value_or(TapKey{}));
    switch (decision) {
    case Decision::Tap:
        // Swallow the real key-up and replay it after the mask key. If that fails,
        // fall back to a plain key press.
        if (ReplayMaskedKeyUp(kb.vkCode)) {
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
        const bool wasHeld = g_detector.TapKeyHeld();
        g_detector.OnMouseInput();
        if (wasHeld) {
            OUTATIME_TRACE(L"hook  mouse input while tap key held -> combination");
        }
        UpdateMouseHook(wasHeld);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// Filled in by the hook thread before it signals `ready`. Lives on the stack of
// StartTapHook, which waits for the signal, so it must not be touched afterwards.
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
        // A new tap key press may have arrived since the removal was requested.
        if (msg.message == WM_UPDATE_MOUSE_HOOK && !g_detector.TapKeyHeld() && g_mouseHook) {
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

bool StartTapHook(HWND target, UINT tapMessage, UINT doubleTapMessage) {
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

void StopTapHook() {
    if (!g_thread) {
        return;
    }
    PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    g_thread = nullptr;
}

void SetTapKeys(TapKeySet keys) { g_detector.SetWatchedKeys(keys); }

void SetDoubleTapMs(DWORD ms) { g_detector.SetDoubleTapMs(ms); }

void SetAcceptInjectedKeys(bool accept) { g_detector.SetAcceptInjected(accept); }

}  // namespace outatime
