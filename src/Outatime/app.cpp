#include "app.hpp"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <string>

#include "full_screen.hpp"
#include "ime.hpp"
#include "resource.h"
#include "settings_app.hpp"
#include "settings_file.hpp"
#include "settings_watcher.hpp"
#include "shared_names.hpp"
#include "trace.hpp"
#include "win_util.hpp"

namespace outatime {
namespace {

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
#endif

}  // namespace

App* App::current_ = nullptr;

App::App(HINSTANCE instance) : instance_(instance) {
    current_ = this;
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    settings_ = LoadSettings().value_or(Settings{});
    status_ = CurrentStatus();
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &activeIcon_);
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_PAUSED), LIM_SMALL, &pausedIcon_);
}

App::~App() {
    DestroyIcon(activeIcon_);
    DestroyIcon(pausedIcon_);
    current_ = nullptr;
}

int App::Run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // A hidden top-level window (not message-only) so it receives TaskbarCreated broadcasts
    // and the settings app can find it with FindWindow.
    if (!CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                         instance_, this)) {
        return 1;
    }
    tray_.emplace(hwnd_, kTrayIconId, WM_TRAYICON);
    UpdateTrayIcon();
    tray_->Add();

    // Tell the settings app whether we run elevated, and let it close us (to restart
    // with the other privilege level) and Explorer reach us even when we do.
    if (IsElevated()) {
        SetPropW(hwnd_, kElevatedProperty, reinterpret_cast<HANDLE>(1));
    }
    ChangeWindowMessageFilterEx(hwnd_, WM_CLOSE, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);

    UpdateStatus();
    if (!StartAltTapHook(hwnd_, WM_ALT_TAP, WM_ALT_DOUBLE_TAP)) {
        MessageBoxW(nullptr, LoadResourceString(IDS_HOOK_FAILED).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd_);
    }

    HWINEVENTHOOK foregroundHook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                        OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
    SetTimer(hwnd_, kStatusTimerId, kStatusIntervalMs, nullptr);

    MSG msg{};
    {
        SettingsWatcher watcher;
        HANDLE watchEvent = watcher.Event();
        const DWORD watchCount = watchEvent ? 1 : 0;
        for (;;) {
            const DWORD result = MsgWaitForMultipleObjectsEx(watchCount, &watchEvent, INFINITE,
                                                             QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (watchCount == 1 && result == WAIT_OBJECT_0) {
                SetTimer(hwnd_, kReloadTimerId, kReloadDelayMs, nullptr);
                watcher.Rearm();
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
    }

    if (foregroundHook) {
        UnhookWinEvent(foregroundHook);
    }
    StopAltTapHook();
    trace::Flush();
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        return app->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CALLBACK App::OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (current_) {
        current_->UpdateStatus();
    }
}

LRESULT App::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            OpenSettingsApp(hwnd_);
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
            break;
        }
        return 0;

    case WM_ALT_TAP:
        OUTATIME_TRACE(L"main  tap received (posted %ld ms ago by tick count)",
                       static_cast<long>(GetTickCount() - GetMessageTime()));
        OnAltTap(static_cast<AltKey>(wParam), reinterpret_cast<HWND>(lParam));
        return 0;

    case WM_ALT_DOUBLE_TAP:
        OUTATIME_TRACE(L"main  double tap received, restoring");
        OnAltDoubleTap();
        return 0;

    case WM_TIMER:
        OnTimer(wParam);
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;

    case WM_DESTROY:
        RemovePropW(hwnd_, kElevatedProperty);
        if (tray_) {
            tray_->Remove();
        }
        PostQuitMessage(0);
        return 0;

    default:
        if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_ && tray_) {
            tray_->Add();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void App::OnTimer(UINT_PTR id) {
    switch (id) {
    case kReloadTimerId:
        KillTimer(hwnd_, kReloadTimerId);
        ReloadSettings();
        break;
    case kStatusTimerId:
        UpdateStatus();
        trace::Flush();
        break;
    case kRecheckTimerId:
        KillTimer(hwnd_, kRecheckTimerId);
        if (lastTap_.requestedOpen) {
            ime::Recheck(lastTap_.window, *lastTap_.requestedOpen);
        }
        break;
#ifndef NDEBUG
    case kWatchTimerId: {
        const auto open = ime::GetOpen(lastTap_.window);
        OUTATIME_TRACE(L"watch +%3u ms open=%s", (watchTick_ + 1) * kWatchIntervalMs,
                       open ? (*open ? L"open" : L"closed") : L"?");
        if (++watchTick_ >= kWatchTicks) {
            KillTimer(hwnd_, kWatchTimerId);
        }
        break;
    }
#endif
    }
}

void App::OnCommand(UINT id) {
    switch (id) {
    case kMenuSettings:
        OpenSettingsApp(hwnd_);
        break;
    case kMenuPause:
        pausedFromMenu_ = !pausedFromMenu_;
        UpdateStatus();
        break;
    case kMenuExit:
        CloseSettingsApp();
        DestroyWindow(hwnd_);
        break;
    }
}

void App::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuSettings, LoadResourceString(IDS_MENU_SETTINGS).c_str());
    AppendMenuW(menu, MF_STRING | (pausedFromMenu_ ? MF_CHECKED : MF_UNCHECKED), kMenuPause,
                LoadResourceString(IDS_MENU_PAUSE).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, LoadResourceString(IDS_MENU_EXIT).c_str());
    SetMenuDefaultItem(menu, kMenuSettings, FALSE);
    tray_->ShowMenu(menu, point);
    DestroyMenu(menu);
}

App::Status App::CurrentStatus() const {
    if (settings_.mode == Mode::Disabled) {
        return Status::Off;
    }
    if (pausedFromMenu_) {
        return Status::Paused;
    }
    if (IsFullScreenAppInFront(settings_.pauseInFullScreen)) {
        return Status::PausedFullScreen;
    }
    return Status::Active;
}

// Re-evaluates whether Alt taps should switch the IME, and tells the hook and
// the tray icon. A paused hook lets Alt through untouched, so games get it as is.
void App::UpdateStatus() {
    const Status status = CurrentStatus();
    const bool active = status == Status::Active;
    SetAltTapKeys(active, active && settings_.mode == Mode::LeftOffRightOn);
    SetDoubleTapMs(settings_.doubleTap ? settings_.doubleTapMs : 0);
    if (status != status_) {
        status_ = status;
        UpdateTrayIcon();
    }
}

void App::UpdateTrayIcon() {
    std::wstring tooltip = kAppName;
    switch (status_) {
    case Status::Active:
        break;
    case Status::Paused:
        tooltip = LoadResourceString(IDS_TIP_PAUSED);
        break;
    case Status::PausedFullScreen:
        tooltip = LoadResourceString(IDS_TIP_FULL_SCREEN);
        break;
    case Status::Off:
        tooltip = LoadResourceString(IDS_TIP_OFF);
        break;
    }
    tray_->Set(status_ == Status::Active ? activeIcon_ : pausedIcon_, tooltip);
}

// Keeps the current settings if the file is unreadable, e.g. while being edited.
void App::ReloadSettings() {
    if (const auto settings = LoadSettings()) {
        settings_ = *settings;
        UpdateStatus();
    }
}

// `foreground` is the window that was in front when the Alt key was released.
void App::OnAltTap(AltKey key, HWND foreground) {
    std::optional<ime::Action> action;
    switch (settings_.mode) {
    case Mode::Toggle:
        if (key == AltKey::Left) {
            action = ime::Action::Toggle;
        }
        break;
    case Mode::LeftOffRightOn:
        action = key == AltKey::Left ? ime::Action::Off : ime::Action::On;
        break;
    case Mode::Disabled:
        break;
    }
    if (!action) {
        return;
    }
    const auto result = ime::Apply(*action, foreground);
    lastTap_ = {foreground, result.previous, result.requested};
    if (result.requested) {
        SetTimer(hwnd_, kRecheckTimerId, kRecheckDelayMs, nullptr);
#ifndef NDEBUG
        watchTick_ = 0;
        SetTimer(hwnd_, kWatchTimerId, kWatchIntervalMs, nullptr);
#endif
    }
}

// The user pressed the same Alt key again right after a tap to get a plain Alt,
// so put the IME back the way it was before the tap.
void App::OnAltDoubleTap() {
    if (lastTap_.previousOpen) {
        ime::SetOpen(lastTap_.window, *lastTap_.previousOpen);
    }
    lastTap_ = {};
    KillTimer(hwnd_, kRecheckTimerId);
}

}  // namespace outatime
