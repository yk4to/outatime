#pragma once

#include <windows.h>

namespace outatime {

// Starts the settings app next to this exe, or tells the user it is missing.
// If it is already open, it brings its window to the front itself.
void OpenSettingsApp(HWND owner);

// Closes any open settings app windows. Exiting from the tray quits all of
// Outatime; other exits (a restart from the settings app) leave it open.
void CloseSettingsApp();

}  // namespace outatime
