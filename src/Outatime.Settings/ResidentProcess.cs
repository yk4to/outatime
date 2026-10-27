using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Outatime.Settings;

// Talks to the running resident process (Outatime.exe) through its hidden window.
// The names below must match main.cpp.
internal static partial class ResidentProcess
{
    private const string WindowClass = "Outatime.Resident";
    private const string MutexName = @"Local\Outatime.Resident";
    private const string ElevatedProperty = "Outatime.Elevated";
    private const uint WmClose = 0x0010;

    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(5);

    // Whether the resident runs as administrator, or null if it is not running.
    public static bool? IsElevated()
    {
        var hwnd = FindWindowW(WindowClass, null);
        return hwnd == 0 ? null : GetPropW(hwnd, ElevatedProperty) != 0;
    }

    public static async Task RestartAsync(bool elevated)
    {
        var hwnd = FindWindowW(WindowClass, null);
        if (hwnd != 0 && !PostMessageW(hwnd, WmClose, 0, 0))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
        // A new instance exits right away while the old one still holds the mutex.
        if (!await WaitUntilAsync(() => !MutexExists()))
        {
            throw new TimeoutException("Outatime did not exit.");
        }
        await StartAsync(elevated);
    }

    public static async Task StartAsync(bool elevated)
    {
        if (elevated)
        {
            await StartupManager.RunTaskAsync();
        }
        else
        {
            // Explorer starts it without elevation even if this app runs elevated.
            Process.Start("explorer.exe", $"\"{StartupManager.ResidentPath}\"")?.Dispose();
        }
        if (!await WaitUntilAsync(() => FindWindowW(WindowClass, null) != 0))
        {
            throw new TimeoutException("Outatime did not start.");
        }
    }

    private static bool MutexExists()
    {
        try
        {
            if (Mutex.TryOpenExisting(MutexName, out var mutex))
            {
                mutex.Dispose();
                return true;
            }
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            // Owned by an elevated instance.
            return true;
        }
    }

    private static async Task<bool> WaitUntilAsync(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + Timeout;
        while (!condition())
        {
            if (DateTime.UtcNow > deadline)
            {
                return false;
            }
            await Task.Delay(100);
        }
        return true;
    }

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint FindWindowW(string className, string? windowName);

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint GetPropW(nint hwnd, string name);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool PostMessageW(nint hwnd, uint msg, nint wParam, nint lParam);
}
