#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <string>

#include "alt_tap_hook.hpp"
#include "ime.hpp"
#include "resource.h"
#include "settings.hpp"

namespace {

using outatime::AltKey;
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

enum MenuId : UINT {
    kMenuSettings = 1,
    kMenuExit,
};

// Editors and the settings app may write the file in several steps, so wait
// for changes to settle before reloading.
constexpr UINT_PTR kReloadTimerId = 1;
constexpr UINT kReloadDelayMs = 200;

// Broadcast by Explorer when the taskbar is (re)created, e.g. after Explorer restarts.
UINT g_taskbarCreatedMsg = 0;

outatime::Settings g_settings;

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

void ApplyMode(Mode mode) {
    outatime::SetAltTapKeys(mode != Mode::Disabled, mode == Mode::LeftOffRightOn);
}

// Keeps the current settings if the file is unreadable, e.g. while being edited.
void ReloadSettings() {
    if (const auto settings = outatime::LoadSettings()) {
        g_settings = *settings;
        ApplyMode(g_settings.mode);
    }
}

// `foreground` is the window that was in front when the Alt key was released.
void OnAltTap(Mode mode, AltKey key, HWND foreground) {
    switch (mode) {
    case Mode::Toggle:
        if (key == AltKey::Left) {
            outatime::ime::Apply(outatime::ime::Action::Toggle, foreground);
        }
        break;
    case Mode::LeftOffRightOn:
        outatime::ime::Apply(
            key == AltKey::Left ? outatime::ime::Action::Off : outatime::ime::Action::On,
            foreground);
        break;
    case Mode::Disabled:
        break;
    }
}

bool AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAYICON;
    // TODO: Replace with app icons that follow the taskbar light/dark theme.
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, kAppName);
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

// Picks the string for the user's UI language from Outatime.rc.
std::wstring LoadResourceString(UINT id) {
    const wchar_t* text = nullptr;
    const int length =
        LoadStringW(GetModuleHandleW(nullptr), id, reinterpret_cast<LPWSTR>(&text), 0);
    return std::wstring(text, length > 0 ? length : 0);
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
        OnAltTap(g_settings.mode, static_cast<AltKey>(wParam), reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_TIMER:
        if (wParam == kReloadTimerId) {
            KillTimer(hwnd, kReloadTimerId);
            ReloadSettings();
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kMenuSettings:
            OpenSettings(hwnd);
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

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // A hidden top-level window (not message-only) so it receives TaskbarCreated broadcasts
    // and the settings app can find it with FindWindow.
    HWND hwnd = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                                nullptr, instance, nullptr);
    if (!hwnd) {
        return 1;
    }

    // Tell the settings app whether we run elevated, and let it close us (to restart
    // with the other privilege level) and Explorer reach us even when we do.
    if (IsElevated()) {
        SetPropW(hwnd, kElevatedProperty, reinterpret_cast<HANDLE>(1));
    }
    ChangeWindowMessageFilterEx(hwnd, WM_CLOSE, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, g_taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);

    g_settings = outatime::LoadSettings().value_or(outatime::Settings{});
    ApplyMode(g_settings.mode);
    if (!outatime::StartAltTapHook(hwnd, WM_ALT_TAP)) {
        MessageBoxW(nullptr, LoadResourceString(IDS_HOOK_FAILED).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
    }

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
            SetTimer(hwnd, kReloadTimerId, kReloadDelayMs, nullptr);
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
    outatime::StopAltTapHook();
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
