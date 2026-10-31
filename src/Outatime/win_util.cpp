#include "win_util.hpp"

#include <appmodel.h>

namespace outatime {

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

std::wstring LoadResourceString(UINT id) {
    const wchar_t* text = nullptr;
    const int length =
        LoadStringW(GetModuleHandleW(nullptr), id, reinterpret_cast<LPWSTR>(&text), 0);
    return std::wstring(text, length > 0 ? length : 0);
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::wstring full(path, len);
    return full.substr(0, full.find_last_of(L'\\'));
}

std::wstring PackageFamilyName() {
    UINT32 length = 0;
    if (GetCurrentPackageFamilyName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
        return {};
    }
    std::wstring name(length, L'\0');
    if (GetCurrentPackageFamilyName(&length, name.data()) != ERROR_SUCCESS) {
        return {};
    }
    name.resize(length - 1);  // The length includes the null terminator.
    return name;
}

}  // namespace outatime
