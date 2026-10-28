#include <windows.h>

#include "app.hpp"
#include "shared_names.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, outatime::kMutexName);
    // ERROR_ACCESS_DENIED: an elevated instance owns the mutex.
    if (const DWORD error = GetLastError();
        error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED) {
        return 0;
    }

    int exitCode = 0;
    {
        outatime::App app(instance);
        exitCode = app.Run();
    }
    CloseHandle(mutex);
    return exitCode;
}
