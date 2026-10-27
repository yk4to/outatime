#pragma once

#include <windows.h>

#include <string_view>

namespace outatime::diagnostics {

// Records a failure to %LOCALAPPDATA%\Outatime\diagnostics.log and the debugger
// output, along with the process name and window class of `window` (never its
// title, which may contain personal data). Call from the main thread only.
void LogFailure(std::wstring_view event, HWND window, std::wstring_view detail = {});

}  // namespace outatime::diagnostics
