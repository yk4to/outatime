#pragma once

#include <windows.h>

#include <optional>

#include "settings.hpp"
#include "tap_hook.hpp"
#include "tray_icon.hpp"

namespace outatime {

// The resident process: a hidden window that owns the tray icon, switches the
// IME on key taps reported by the hook, and follows settings.json. Create one
// per process, after making sure no other instance is running.
class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs until the user exits. Returns the process exit code.
    int Run();

private:
    // Whether taps switch the IME right now, shown by the tray icon and tooltip.
    enum class Status {
        Active,
        Paused,            // From the tray menu
        PausedFullScreen,  // A full-screen app is in front
        Off,               // No key does anything in the settings
    };

    // What the last tap changed, so a double tap can put it back and the
    // recheck knows what to expect.
    struct LastTap {
        HWND window = nullptr;
        std::optional<bool> previousOpen;
        std::optional<bool> requestedOpen;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnTimer(UINT_PTR id);
    void OnCommand(UINT id);
    void ShowTrayMenu(POINT point);

    TapKeySet AssignedKeys() const;
    Status CurrentStatus() const;
    void UpdateStatus();
    void UpdateTrayIcon();
    void ReloadSettings();

    void OnTap(TapKey key, HWND foreground);
    void OnDoubleTap();

    // SetWinEventHook callbacks get no context, and there is only one App.
    static App* current_;

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    // Broadcast by Explorer when the taskbar is (re)created, e.g. after it restarts.
    UINT taskbarCreatedMsg_ = 0;
    HICON activeIcon_ = nullptr;
    HICON pausedIcon_ = nullptr;
    std::optional<TrayIcon> tray_;

    Settings settings_;
    bool pausedFromMenu_ = false;
    Status status_ = Status::Active;
    LastTap lastTap_;
#ifndef NDEBUG
    int watchTick_ = 0;
#endif
};

}  // namespace outatime
