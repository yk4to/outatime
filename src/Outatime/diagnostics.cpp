#include "diagnostics.hpp"

#include <shlobj.h>

#include <cwchar>
#include <filesystem>
#include <string>

namespace outatime::diagnostics {

// Not under %APPDATA%\Outatime: the resident watches that directory for settings changes.
std::filesystem::path LogDirectory() {
    PWSTR localAppData = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(
            SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) {
        path = std::filesystem::path(localAppData) / L"Outatime";
    }
    CoTaskMemFree(localAppData);
    return path;
}

namespace {

// Keep the log small: once it grows past this, the previous one is kept as .old.
constexpr ULONGLONG kMaxLogBytes = 64 * 1024;

std::wstring ProcessName(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    // Limited access works even for elevated processes.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return L"?";
    }
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    return ok ? std::filesystem::path(path).filename().wstring() : L"?";
}

std::wstring ClassName(HWND window) {
    wchar_t name[256];
    const int length = GetClassNameW(window, name, ARRAYSIZE(name));
    return length > 0 ? std::wstring(name, length) : L"?";
}

void AppendToFile(const std::wstring& line) {
    const auto dir = LogDirectory();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / L"diagnostics.log";
    if (std::filesystem::file_size(path, ec) > kMaxLogBytes && !ec) {
        MoveFileExW(path.c_str(), (dir / L"diagnostics.old.log").c_str(),
                    MOVEFILE_REPLACE_EXISTING);
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), bytes,
                        nullptr, nullptr);

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

}  // namespace

void LogFailure(std::wstring_view event, HWND window, std::wstring_view detail) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t timestamp[32];
    swprintf_s(timestamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

    std::wstring line = timestamp;
    line += L"  ";
    line += event;
    if (window) {
        line += L"  process=" + ProcessName(window) + L" class=" + ClassName(window);
    }
    if (!detail.empty()) {
        line += L"  ";
        line += detail;
    }
    line += L"\r\n";

    OutputDebugStringW((L"Outatime: " + line).c_str());
    AppendToFile(line);
}

}  // namespace outatime::diagnostics
