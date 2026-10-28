#pragma once

#include <windows.h>

#include <string>

namespace outatime {

bool IsElevated();

// Picks the string for the user's UI language from Outatime.rc.
std::wstring LoadResourceString(UINT id);

// The directory of this exe, without a trailing backslash.
std::wstring ExeDirectory();

}  // namespace outatime
