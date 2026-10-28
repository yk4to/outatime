#include "trace.hpp"

#include <windows.h>

#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#include "diagnostics.hpp"

namespace outatime::trace {

#ifdef NDEBUG

void Write(const wchar_t*, ...) {}

void Flush() {}

#else

namespace {

std::mutex g_mutex;
std::vector<std::wstring> g_lines;

// Milliseconds since the first trace, from the high-resolution counter.
double Now() {
    static const LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return value;
    }();
    static const LARGE_INTEGER start = [] {
        LARGE_INTEGER value;
        QueryPerformanceCounter(&value);
        return value;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 /
           static_cast<double>(frequency.QuadPart);
}

}  // namespace

void Write(const wchar_t* format, ...) {
    const double now = Now();
    wchar_t text[256];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, _TRUNCATE, format, args);
    va_end(args);

    wchar_t line[320];
    swprintf_s(line, L"%10.3f  tid=%-6lu %s\r\n", now, GetCurrentThreadId(), text);
    std::lock_guard lock(g_mutex);
    g_lines.emplace_back(line);
}

void Flush() {
    std::vector<std::wstring> lines;
    {
        std::lock_guard lock(g_mutex);
        lines.swap(g_lines);
    }
    if (lines.empty()) {
        return;
    }

    std::wstring text;
    for (const auto& line : lines) {
        text += line;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), bytes,
                        nullptr, nullptr);

    const auto dir = diagnostics::LogDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    HANDLE file = CreateFileW((dir / L"trace.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

#endif

}  // namespace outatime::trace
