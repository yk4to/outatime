#pragma once

// Timing trace for debugging, written to %LOCALAPPDATA%\Outatime\trace.log.
// Only in Debug builds: OUTATIME_TRACE compiles to nothing in Release, so the
// arguments are not even evaluated there. Never trace which key was pressed.
//
//   OUTATIME_TRACE(L"main tap received key=%d", key);

#ifdef NDEBUG
#define OUTATIME_TRACE(...) ((void)0)
#else
#define OUTATIME_TRACE(...) ::outatime::trace::Write(__VA_ARGS__)
#endif

#include <sal.h>

namespace outatime::trace {

// Safe to call from any thread, and cheap enough for the hook thread: it only
// appends to a buffer in memory.
void Write(_Printf_format_string_ const wchar_t* format, ...);

// Appends buffered lines to the file. Call from the main thread now and then,
// and before exiting. Does nothing in Release.
void Flush();

}  // namespace outatime::trace
