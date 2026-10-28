#include "settings_watcher.hpp"

#include <filesystem>

#include "settings.hpp"

namespace outatime {

SettingsWatcher::SettingsWatcher() {
    const auto dir = SettingsDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const HANDLE handle = FindFirstChangeNotificationW(
        dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    if (handle != INVALID_HANDLE_VALUE) {
        handle_ = handle;
    }
}

SettingsWatcher::~SettingsWatcher() {
    if (handle_) {
        FindCloseChangeNotification(handle_);
    }
}

void SettingsWatcher::Rearm() {
    if (handle_) {
        FindNextChangeNotification(handle_);
    }
}

}  // namespace outatime
