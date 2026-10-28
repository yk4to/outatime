#include "tray_icon.hpp"

#include <shellapi.h>

namespace outatime {

void TrayIcon::Fill(NOTIFYICONDATAW& data) const {
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = id_;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tooltip_.c_str(), _TRUNCATE);
}

bool TrayIcon::Add() {
    NOTIFYICONDATAW data{};
    Fill(data);
    data.uFlags |= NIF_MESSAGE;
    data.uCallbackMessage = callbackMessage_;
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        return false;
    }
    added_ = true;
    data.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
}

void TrayIcon::Remove() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = id_;
    Shell_NotifyIconW(NIM_DELETE, &data);
    added_ = false;
}

void TrayIcon::Set(HICON icon, std::wstring_view tooltip) {
    icon_ = icon;
    tooltip_ = tooltip;
    if (added_) {
        NOTIFYICONDATAW data{};
        Fill(data);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
}

void TrayIcon::ShowMenu(HMENU menu, POINT point) const {
    UINT flags = TPM_RIGHTBUTTON;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

    // Without these the menu does not close when the user clicks elsewhere.
    SetForegroundWindow(window_);
    TrackPopupMenuEx(menu, flags, point.x, point.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
}

}  // namespace outatime
