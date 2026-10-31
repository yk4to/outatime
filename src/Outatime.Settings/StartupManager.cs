using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Security;
using System.Security.Principal;
using System.Text;
using Microsoft.Win32;
using Microsoft.Windows.ApplicationModel.Resources;
using Windows.ApplicationModel;

namespace Outatime.Settings;

internal enum StartupMode
{
    Off,
    Normal,
    Elevated,
}

// Registers the resident process to start at sign-in: a Run key for a normal
// start, or a scheduled task when it should run as administrator. The MSIX
// package can't write the Run key, so it has a startup task in its manifest
// instead, and doesn't offer to run as administrator.
internal static class StartupManager
{
    // Also the TaskId of the startup task in Package.appxmanifest.
    private const string Name = "Outatime";
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";

    private static readonly ResourceLoader Resources = new();

    public static async Task<StartupMode> GetModeAsync()
    {
        if (AppPackage.IsPackaged)
        {
            var task = await StartupTask.GetAsync(Name);
            return task.State is StartupTaskState.Enabled or StartupTaskState.EnabledByPolicy
                ? StartupMode.Normal
                : StartupMode.Off;
        }
        if (TaskExists())
        {
            return StartupMode.Elevated;
        }
        using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath);
        return key?.GetValue(Name) is null ? StartupMode.Off : StartupMode.Normal;
    }

    // Throws Win32Exception with ERROR_CANCELLED if the user declines the UAC prompt.
    public static async Task SetModeAsync(StartupMode mode)
    {
        if (AppPackage.IsPackaged)
        {
            await SetStartupTaskAsync(mode == StartupMode.Normal);
            return;
        }

        // Task changes need a UAC prompt, so do them first: if it is declined, nothing has changed.
        if (mode == StartupMode.Elevated)
        {
            await CreateTaskAsync();
        }
        else if (TaskExists())
        {
            await RunSchtasksElevatedAsync($"/delete /tn {Name} /f");
        }

        using var key = Registry.CurrentUser.CreateSubKey(RunKeyPath);
        if (mode == StartupMode.Normal)
        {
            key.SetValue(Name, $"\"{AppPackage.ResidentPath}\"");
        }
        else
        {
            key.DeleteValue(Name, throwOnMissingValue: false);
        }
    }

    // Users can turn the startup task off in Windows Settings, and only they can turn
    // it back on.
    private static async Task SetStartupTaskAsync(bool enabled)
    {
        var task = await StartupTask.GetAsync(Name);
        if (!enabled)
        {
            task.Disable();
            return;
        }
        switch (await task.RequestEnableAsync())
        {
            case StartupTaskState.Enabled or StartupTaskState.EnabledByPolicy:
                return;
            case StartupTaskState.DisabledByUser:
                throw new InvalidOperationException(Resources.GetString("StartupDisabledByUser"));
            default:
                throw new InvalidOperationException(Resources.GetString("StartupDisabledByPolicy"));
        }
    }

    // Starts the resident process as administrator. The task needs elevation to be
    // created, but not to be run.
    public static async Task RunTaskAsync()
    {
        using var process = StartSchtasks($"/run /tn {Name}");
        await process.WaitForExitAsync();
        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                string.Format(CultureInfo.CurrentCulture, Resources.GetString("SchtasksFailed"), process.ExitCode));
        }
    }

    private static bool TaskExists()
    {
        using var process = StartSchtasks($"/query /tn {Name}");
        process.WaitForExit();
        return process.ExitCode == 0;
    }

    private static Process StartSchtasks(string arguments)
    {
        return Process.Start(new ProcessStartInfo("schtasks.exe", arguments)
        {
            CreateNoWindow = true,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        })!;
    }

    private static async Task CreateTaskAsync()
    {
        var xmlPath = Path.Combine(Path.GetTempPath(), $"Outatime-{Guid.NewGuid():N}.xml");
        await File.WriteAllTextAsync(xmlPath, TaskXml(), Encoding.Unicode);
        try
        {
            await RunSchtasksElevatedAsync($"/create /tn {Name} /xml \"{xmlPath}\" /f");
        }
        finally
        {
            File.Delete(xmlPath);
        }
    }

    private static async Task RunSchtasksElevatedAsync(string arguments)
    {
        using var process = Process.Start(new ProcessStartInfo("schtasks.exe", arguments)
        {
            UseShellExecute = true,
            Verb = "runas",
            WindowStyle = ProcessWindowStyle.Hidden,
        })!;
        await process.WaitForExitAsync();
        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                string.Format(CultureInfo.CurrentCulture, Resources.GetString("SchtasksFailed"), process.ExitCode));
        }
    }

    // The schtasks command-line defaults stop the task on battery and after 72 hours,
    // and run it at below-normal priority, so define it explicitly.
    private static string TaskXml()
    {
        var user = WindowsIdentity.GetCurrent().User!.Value;
        var command = SecurityElement.Escape(AppPackage.ResidentPath);
        return $"""
            <?xml version="1.0" encoding="UTF-16"?>
            <Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
              <Triggers>
                <LogonTrigger>
                  <UserId>{user}</UserId>
                </LogonTrigger>
              </Triggers>
              <Principals>
                <Principal id="Author">
                  <UserId>{user}</UserId>
                  <LogonType>InteractiveToken</LogonType>
                  <RunLevel>HighestAvailable</RunLevel>
                </Principal>
              </Principals>
              <Settings>
                <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
                <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
                <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
                <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
                <Priority>4</Priority>
              </Settings>
              <Actions Context="Author">
                <Exec>
                  <Command>{command}</Command>
                </Exec>
              </Actions>
            </Task>
            """;
    }
}
