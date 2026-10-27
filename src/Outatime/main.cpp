#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"Outatime.Resident";
constexpr wchar_t kMutexName[] = L"Local\\Outatime.Resident";
constexpr wchar_t kAppName[] = L"Outatime";

constexpr UINT kTrayIconId = 1;
constexpr UINT WM_TRAYICON = WM_APP + 1;

enum MenuId : UINT {
    kMenuSettings = 1,
    kMenuExit,
};

// Broadcast by Explorer when the taskbar is (re)created, e.g. after Explorer restarts.
UINT g_taskbarCreatedMsg = 0;

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
        const std::wstring message = L"Settings app not found:\n" + exe;
        MessageBoxW(hwnd, message.c_str(), kAppName, MB_OK | MB_ICONWARNING);
    }
}

void ShowTrayMenu(HWND hwnd, POINT pt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"&Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"E&xit");
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

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kMenuSettings:
            OpenSettings(hwnd);
            return 0;
        case kMenuExit:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
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
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // A hidden top-level window (not message-only) so it receives TaskbarCreated
    // broadcasts and can be found by the settings app with FindWindow.
    HWND hwnd = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED,
                                0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        return 1;
    }

    // Let Explorer reach us even when we run elevated.
    ChangeWindowMessageFilterEx(hwnd, g_taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
