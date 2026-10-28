#include "settings_app.hpp"

#include <shellapi.h>

#include <string>

#include "resource.h"
#include "shared_names.hpp"
#include "win_util.hpp"

namespace outatime {

void OpenSettingsApp(HWND owner) {
    // Distribution layout: Outatime.exe + settings\Outatime.Settings.exe
    const std::wstring exe = ExeDirectory() + L"\\settings\\Outatime.Settings.exe";
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(owner, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        const std::wstring message = LoadResourceString(IDS_SETTINGS_NOT_FOUND) + exe;
        MessageBoxW(owner, message.c_str(), kAppName, MB_OK | MB_ICONWARNING);
    }
}

void CloseSettingsApp() {
    EnumWindows(
        [](HWND window, LPARAM) -> BOOL {
            if (GetPropW(window, kSettingsWindowProperty)) {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            return TRUE;
        },
        0);
}

}  // namespace outatime
