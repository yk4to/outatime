#pragma once

#include <windows.h>

namespace outatime {

// Watches the settings directory (creating it if needed) so edits to
// settings.json can apply without a restart.
class SettingsWatcher {
public:
    SettingsWatcher();
    ~SettingsWatcher();
    SettingsWatcher(const SettingsWatcher&) = delete;
    SettingsWatcher& operator=(const SettingsWatcher&) = delete;

    // Signaled when a file in the directory changes, for MsgWaitForMultipleObjects.
    // Null if the directory could not be watched.
    HANDLE Event() const { return handle_; }

    // Call after Event() was signaled to wait for the next change.
    void Rearm();

private:
    HANDLE handle_ = nullptr;
};

}  // namespace outatime
