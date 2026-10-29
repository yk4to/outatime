#pragma once

#include "version.h"

namespace outatime {

inline constexpr wchar_t kAppName[] = L"Outatime";

// The settings app uses these names too (ResidentProcess.cs).
inline constexpr wchar_t kWindowClass[] = L"Outatime.Resident";
// The window's title is our version, so the settings app can tell whether we are
// the same build: "<version>+<commit>".
inline constexpr wchar_t kWindowTitle[] = L"" OUTATIME_INFORMATIONAL_VERSION;
inline constexpr wchar_t kMutexName[] = L"Local\\Outatime.Resident";
inline constexpr wchar_t kElevatedProperty[] = L"Outatime.Elevated";

// Set by the settings app on its window (MainWindow.xaml.cs).
inline constexpr wchar_t kSettingsWindowProperty[] = L"Outatime.Settings";

}  // namespace outatime
