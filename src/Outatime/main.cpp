#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <optional>
#include <string>

#include "alt_tap_hook.hpp"
#include "ime.hpp"
#include "resource.h"
#include "settings.hpp"
#include "trace.hpp"

namespace {

using outatime::AltKey;
using outatime::FullScreenPause;
using outatime::Mode;

// The settings app uses these names too (ResidentProcess.cs).
constexpr wchar_t kWindowClass[] = L"Outatime.Resident";
constexpr wchar_t kMutexName[] = L"Local\\Outatime.Resident";
constexpr wchar_t kElevatedProperty[] = L"Outatime.Elevated";
// Set by the settings app on its window (MainWindow.xaml.cs).
constexpr wchar_t kSettingsWindowProperty[] = L"Outatime.Settings";
constexpr wchar_t kAppName[] = L"Outatime";

constexpr UINT kTrayIconId = 1;
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_ALT_TAP = WM_APP + 2;
constexpr UINT WM_ALT_DOUBLE_TAP = WM_APP + 3;

enum MenuId : UINT {
    kMenuSettings = 1,
    kMenuPause,
    kMenuExit,
};

// Editors and the settings app may write the file in several steps, so wait
// for changes to settle before reloading.
constexpr UINT_PTR kReloadTimerId = 1;
constexpr UINT kReloadDelayMs = 200;

// Foreground changes are reported right away, but an app can also go full screen
// in place (e.g. F11), so the full-screen state is polled too.
constexpr UINT_PTR kStatusTimerId = 2;
constexpr UINT kStatusIntervalMs = 1000;

// Some apps accept an IME change and undo it a moment later, which the read-back
// right after the change cannot see. Check once more after this long.
constexpr UINT_PTR kRecheckTimerId = 3;
constexpr UINT kRecheckDelayMs = 300;

#ifndef NDEBUG
// Debug builds also trace the IME state every 50 ms for 500 ms after a tap.
constexpr UINT_PTR kWatchTimerId = 4;
constexpr UINT kWatchIntervalMs = 50;
constexpr int kWatchTicks = 10;
int g_watchTick = 0;
#endif

// Whether Alt taps switch the IME right now, shown by the tray icon and tooltip.
enum class Status {
    Active,
    Paused,            // From the tray menu
    PausedFullScreen,  // A full-screen app is in front
    Off,               // Mode is "disabled" in the settings
};

// Broadcast by Explorer when the taskbar is (re)created, e.g. after Explorer restarts.
UINT g_taskbarCreatedMsg = 0;

HWND g_hwnd = nullptr;
outatime::Settings g_settings;
bool g_pausedFromMenu = false;
Status g_status = Status::Active;
// What the last tap changed, so a double tap can put it back.
HWND g_lastTapWindow = nullptr;
std::optional<bool> g_lastTapPreviousOpen;
std::optional<bool> g_lastTapRequestedOpen;
HICON g_activeIcon = nullptr;
HICON g_pausedIcon = nullptr;

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

// Picks the string for the user's UI language from Outatime.rc.
std::wstring LoadResourceString(UINT id) {
    const wchar_t* text = nullptr;
    const int length =
        LoadStringW(GetModuleHandleW(nullptr), id, reinterpret_cast<LPWSTR>(&text), 0);
    return std::wstring(text, length > 0 ? length : 0);
}

// "Games" is the same check as PowerToys' game mode detection. Like PowerToys,
// this trusts the shell rather than checking whether a window covers the
// monitor: that would also pause in full-screen browsers and editors, where
// switching the IME is still wanted.
bool IsFullScreenAppInFront(FullScreenPause setting) {
    if (setting == FullScreenPause::Off) {
        return false;
    }
    QUERY_USER_NOTIFICATION_STATE state;
    if (FAILED(SHQueryUserNotificationState(&state))) {
        return false;
    }
    return state == QUNS_RUNNING_D3D_FULL_SCREEN ||
           (setting == FullScreenPause::All && state == QUNS_BUSY);
}

Status CurrentStatus() {
    if (g_settings.mode == Mode::Disabled) {
        return Status::Off;
    }
    if (g_pausedFromMenu) {
        return Status::Paused;
    }
    if (IsFullScreenAppInFront(g_settings.pauseInFullScreen)) {
        return Status::PausedFullScreen;
    }
    return Status::Active;
}

// Fills the parts of the tray icon that reflect g_status.
void FillTrayIconData(NOTIFYICONDATAW& nid, HWND hwnd) {
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = g_status == Status::Active ? g_activeIcon : g_pausedIcon;

    std::wstring tip = kAppName;
    switch (g_status) {
    case Status::Active:
        break;
    case Status::Paused:
        tip = LoadResourceString(IDS_TIP_PAUSED);
        break;
    case Status::PausedFullScreen:
        tip = LoadResourceString(IDS_TIP_FULL_SCREEN);
        break;
    case Status::Off:
        tip = LoadResourceString(IDS_TIP_OFF);
        break;
    }
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
}

bool AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    FillTrayIconData(nid, hwnd);
    nid.uFlags |= NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        return false;
    }
    nid.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIconW(NIM_SETVERSION, &nid) != FALSE;
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// Re-evaluates whether Alt taps should switch the IME, and tells the hook and
// the tray icon. A paused hook lets Alt through untouched, so games get it as is.
void UpdateStatus() {
    const Status status = CurrentStatus();
    const bool active = status == Status::Active;
    outatime::SetAltTapKeys(active, active && g_settings.mode == Mode::LeftOffRightOn);
    outatime::SetDoubleTapMs(g_settings.doubleTap ? g_settings.doubleTapMs : 0);
    if (status != g_status) {
        g_status = status;
        NOTIFYICONDATAW nid{};
        FillTrayIconData(nid, g_hwnd);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    UpdateStatus();
}

// Keeps the current settings if the file is unreadable, e.g. while being edited.
void ReloadSettings() {
    if (const auto settings = outatime::LoadSettings()) {
        g_settings = *settings;
        UpdateStatus();
    }
}

// `foreground` is the window that was in front when the Alt key was released.
void OnAltTap(Mode mode, AltKey key, HWND foreground) {
    std::optional<outatime::ime::Action> action;
    switch (mode) {
    case Mode::Toggle:
        if (key == AltKey::Left) {
            action = outatime::ime::Action::Toggle;
        }
        break;
    case Mode::LeftOffRightOn:
        action = key == AltKey::Left ? outatime::ime::Action::Off : outatime::ime::Action::On;
        break;
    case Mode::Disabled:
        break;
    }
    if (!action) {
        return;
    }
    const auto result = outatime::ime::Apply(*action, foreground);
    g_lastTapWindow = foreground;
    g_lastTapPreviousOpen = result.previous;
    g_lastTapRequestedOpen = result.requested;
    if (result.requested) {
        SetTimer(g_hwnd, kRecheckTimerId, kRecheckDelayMs, nullptr);
#ifndef NDEBUG
        g_watchTick = 0;
        SetTimer(g_hwnd, kWatchTimerId, kWatchIntervalMs, nullptr);
#endif
    }
}

// The user pressed the same Alt key again right after a tap to get a plain Alt,
// so put the IME back the way it was before the tap.
void OnAltDoubleTap() {
    if (g_lastTapPreviousOpen) {
        outatime::ime::SetOpen(g_lastTapWindow, *g_lastTapPreviousOpen);
    }
    g_lastTapWindow = nullptr;
    g_lastTapPreviousOpen.reset();
    g_lastTapRequestedOpen.reset();
    KillTimer(g_hwnd, kRecheckTimerId);
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::wstring full(path, len);
    return full.substr(0, full.find_last_of(L'\\'));
}

void OpenSettings(HWND hwnd) {
    // Distribution layout: Outatime.exe + settings\Outatime.Settings.exe
    const std::wstring exe = ExeDirectory() + L"\\settings\\Outatime.Settings.exe";
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(hwnd, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        const std::wstring message = LoadResourceString(IDS_SETTINGS_NOT_FOUND) + exe;
        MessageBoxW(hwnd, message.c_str(), kAppName, MB_OK | MB_ICONWARNING);
    }
}

// Exiting from the tray quits all of Outatime, so close the settings app too.
// Other exits (a restart from the settings app) leave it open.
void CloseSettingsWindows() {
    EnumWindows(
        [](HWND window, LPARAM) -> BOOL {
            if (GetPropW(window, kSettingsWindowProperty)) {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            return TRUE;
        },
        0);
}

void ShowTrayMenu(HWND hwnd, POINT pt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuSettings, LoadResourceString(IDS_MENU_SETTINGS).c_str());
    AppendMenuW(menu, MF_STRING | (g_pausedFromMenu ? MF_CHECKED : MF_UNCHECKED), kMenuPause,
                LoadResourceString(IDS_MENU_PAUSE).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, LoadResourceString(IDS_MENU_EXIT).c_str());
    SetMenuDefaultItem(menu, kMenuSettings, FALSE);

    UINT flags = TPM_RIGHTBUTTON;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

    // Without these the menu does not close when the user clicks elsewhere.
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(menu, flags, pt.x, pt.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        return 0;

    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            OpenSettings(hwnd);
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd, {GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            break;
        }
        return 0;

    case WM_ALT_TAP:
        OUTATIME_TRACE(L"main  tap received (posted %ld ms ago by tick count)",
                       static_cast<long>(GetTickCount() - GetMessageTime()));
        OnAltTap(g_settings.mode, static_cast<AltKey>(wParam), reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_ALT_DOUBLE_TAP:
        OUTATIME_TRACE(L"main  double tap received, restoring");
        OnAltDoubleTap();
        return 0;

    case WM_TIMER:
        if (wParam == kReloadTimerId) {
            KillTimer(hwnd, kReloadTimerId);
            ReloadSettings();
            return 0;
        }
        if (wParam == kStatusTimerId) {
            UpdateStatus();
            outatime::trace::Flush();
            return 0;
        }
        if (wParam == kRecheckTimerId) {
            KillTimer(hwnd, kRecheckTimerId);
            if (g_lastTapRequestedOpen) {
                outatime::ime::Recheck(g_lastTapWindow, *g_lastTapRequestedOpen);
            }
            return 0;
        }
#ifndef NDEBUG
        if (wParam == kWatchTimerId) {
            const auto open = outatime::ime::GetOpen(g_lastTapWindow);
            OUTATIME_TRACE(L"watch +%3u ms open=%s", (g_watchTick + 1) * kWatchIntervalMs,
                           open ? (*open ? L"open" : L"closed") : L"?");
            if (++g_watchTick >= kWatchTicks) {
                KillTimer(hwnd, kWatchTimerId);
            }
            return 0;
        }
#endif
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kMenuSettings:
            OpenSettings(hwnd);
            return 0;
        case kMenuPause:
            g_pausedFromMenu = !g_pausedFromMenu;
            UpdateStatus();
            return 0;
        case kMenuExit:
            CloseSettingsWindows();
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        RemovePropW(hwnd, kElevatedProperty);
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        return 0;

    default:
        if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg) {
            AddTrayIcon(hwnd);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    // ERROR_ACCESS_DENIED: an elevated instance owns the mutex.
    if (const DWORD error = GetLastError();
        error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED) {
        return 0;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Loaded before the window so WM_CREATE adds the tray icon in the right state.
    g_settings = outatime::LoadSettings().value_or(outatime::Settings{});
    g_status = CurrentStatus();
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &g_activeIcon);
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_PAUSED), LIM_SMALL, &g_pausedIcon);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // A hidden top-level window (not message-only) so it receives TaskbarCreated broadcasts
    // and the settings app can find it with FindWindow.
    g_hwnd = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                             instance, nullptr);
    if (!g_hwnd) {
        return 1;
    }

    // Tell the settings app whether we run elevated, and let it close us (to restart
    // with the other privilege level) and Explorer reach us even when we do.
    if (IsElevated()) {
        SetPropW(g_hwnd, kElevatedProperty, reinterpret_cast<HANDLE>(1));
    }
    ChangeWindowMessageFilterEx(g_hwnd, WM_CLOSE, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, g_taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);

    UpdateStatus();
    if (!outatime::StartAltTapHook(g_hwnd, WM_ALT_TAP, WM_ALT_DOUBLE_TAP)) {
        MessageBoxW(nullptr, LoadResourceString(IDS_HOOK_FAILED).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        DestroyWindow(g_hwnd);
    }

    HWINEVENTHOOK foregroundHook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                        OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
    SetTimer(g_hwnd, kStatusTimerId, kStatusIntervalMs, nullptr);

    // Watch the settings directory so edits apply without a restart.
    const auto settingsDir = outatime::SettingsDirectory();
    std::error_code ec;
    std::filesystem::create_directories(settingsDir, ec);
    HANDLE watch = FindFirstChangeNotificationW(
        settingsDir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    const DWORD watchCount = watch != INVALID_HANDLE_VALUE ? 1 : 0;

    MSG msg{};
    for (;;) {
        const DWORD result = MsgWaitForMultipleObjectsEx(watchCount, &watch, INFINITE, QS_ALLINPUT,
                                                         MWMO_INPUTAVAILABLE);
        if (watchCount == 1 && result == WAIT_OBJECT_0) {
            SetTimer(g_hwnd, kReloadTimerId, kReloadDelayMs, nullptr);
            FindNextChangeNotification(watch);
            continue;
        }
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) {
            break;
        }
    }

    if (watch != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(watch);
    }
    if (foregroundHook) {
        UnhookWinEvent(foregroundHook);
    }
    outatime::StopAltTapHook();
    outatime::trace::Flush();
    DestroyIcon(g_activeIcon);
    DestroyIcon(g_pausedIcon);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
