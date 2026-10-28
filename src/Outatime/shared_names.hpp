#pragma once

namespace outatime {

inline constexpr wchar_t kAppName[] = L"Outatime";

// The settings app uses these names too (ResidentProcess.cs).
inline constexpr wchar_t kWindowClass[] = L"Outatime.Resident";
inline constexpr wchar_t kMutexName[] = L"Local\\Outatime.Resident";
inline constexpr wchar_t kElevatedProperty[] = L"Outatime.Elevated";

// Set by the settings app on its window (MainWindow.xaml.cs).
inline constexpr wchar_t kSettingsWindowProperty[] = L"Outatime.Settings";

}  // namespace outatime
