using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace Outatime.Settings;

public sealed partial class MainWindow : Window
{
    private const int DefaultWidth = 560;
    private const int DefaultHeight = 480;

    public MainWindow()
    {
        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        // AppWindow.Resize takes physical pixels, so scale by the window's DPI.
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var scale = GetDpiForWindow(hwnd) / 96.0;
        AppWindow.Resize(new SizeInt32((int)(DefaultWidth * scale), (int)(DefaultHeight * scale)));

        // TODO: Load and save %APPDATA%\Outatime\settings.json, then notify the resident process.
    }

    [LibraryImport("user32.dll")]
    private static partial uint GetDpiForWindow(nint hwnd);
}
