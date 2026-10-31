using System.Runtime.InteropServices;
using Windows.ApplicationModel;

namespace Outatime.Settings;

// How Outatime was installed: from the zip, with Outatime.exe + settings\Outatime.Settings.exe,
// or from the MSIX package, with both exes at the package's root.
internal static partial class AppPackage
{
    private const int AppModelErrorNoPackage = 15700;

    // Whether this app runs with package identity, that is, from the MSIX package.
    public static bool IsPackaged { get; } = HasPackageIdentity();

    // Where Outatime.exe and THIRD-PARTY-NOTICES.txt are.
    public static string ResidentDirectory { get; } = IsPackaged
        ? AppContext.BaseDirectory
        : Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));

    public static string ResidentPath => Path.Combine(ResidentDirectory, "Outatime.exe");

    // What to pass to explorer.exe to start Outatime.exe. In the package, its exe started
    // by path runs without package identity, and so with the zip's settings file, so start
    // the app by its ID in Package.appxmanifest instead.
    public static string ResidentLaunchTarget => IsPackaged
        ? $@"shell:AppsFolder\{Package.Current.Id.FamilyName}!Outatime"
        : $"\"{ResidentPath}\"";

    private static unsafe bool HasPackageIdentity()
    {
        uint length = 0;
        return GetCurrentPackageFullName(ref length, null) != AppModelErrorNoPackage;
    }

    [LibraryImport("kernel32.dll")]
    private static unsafe partial int GetCurrentPackageFullName(ref uint packageFullNameLength, char* packageFullName);
}
