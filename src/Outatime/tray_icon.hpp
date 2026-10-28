#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <string_view>

namespace outatime {

// A notification area (tray) icon that posts `callbackMessage` to `window` in the
// NOTIFYICON_VERSION_4 format. It only shows what it is told to.
class TrayIcon {
public:
    TrayIcon(HWND window, UINT id, UINT callbackMessage)
        : window_(window), id_(id), callbackMessage_(callbackMessage) {}

    // Adds the icon. Call again when Explorer broadcasts TaskbarCreated after a
    // restart, which removes every tray icon.
    bool Add();
    void Remove();

    // Changes the icon and tooltip, now if the icon was added and on later Add()s.
    void Set(HICON icon, std::wstring_view tooltip);

    // Shows `menu` at `point`. The chosen item arrives as WM_COMMAND.
    void ShowMenu(HMENU menu, POINT point) const;

private:
    void Fill(NOTIFYICONDATAW& data) const;

    HWND window_;
    UINT id_;
    UINT callbackMessage_;
    HICON icon_ = nullptr;
    std::wstring tooltip_;
    bool added_ = false;
};

}  // namespace outatime
