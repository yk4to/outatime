using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.ApplicationModel.Resources;
using Windows.Graphics;

namespace Outatime.Settings;

public sealed partial class MainWindow : Window
{
    private const int DefaultWidth = 640;
    private const int DefaultHeight = 520;
    private const int ErrorCancelled = 1223;
    // The resident process closes windows with this property when the user exits it (settings_app.cpp).
    private const string WindowProperty = "Outatime.Settings";

    // This app's commit. The .NET SDK appends it to the informational version: "0.1.0+<sha>".
    private static readonly string? Commit = CommitOf(Assembly.GetExecutingAssembly()
        .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion);

    private readonly ResourceLoader _resources = new();
    private readonly nint _hwnd;
    private readonly ResidentMonitor _resident;

    // Set while the code changes a selection, so the change handlers ignore it.
    private bool _updatingSelection;
    private StartupMode _startupMode;

    public MainWindow()
    {
        InitializeComponent();

        Title = _resources.GetString("AppTitle");
        AppTitleBar.Title = Title;
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        // AppWindow.Resize takes physical pixels, so scale by the window's DPI.
        _hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var scale = GetDpiForWindow(_hwnd) / 96.0;
        AppWindow.Resize(new SizeInt32((int)(DefaultWidth * scale), (int)(DefaultHeight * scale)));

        _startupMode = StartupManager.GetMode();
        _updatingSelection = true;
        StartupComboBox.SelectedIndex = (int)_startupMode;
        _updatingSelection = false;
        LoadSettingsFile();

        SettingsFileCard.Description = SettingsStore.FilePath;
        ShowBuildInfo();

        SetPropW(_hwnd, WindowProperty, 1);
        Closed += (_, _) => RemovePropW(_hwnd, WindowProperty);

        _resident = new ResidentMonitor(DispatcherQueue);
        _resident.Changed += (_, _) => UpdateResidentInfoBars();
        UpdateResidentInfoBars();

        // The settings file may have been edited while this window was in the background.
        Activated += (_, args) =>
        {
            if (args.WindowActivationState != WindowActivationState.Deactivated)
            {
                LoadSettingsFile();
            }
        };
    }

    private void LoadSettingsFile()
    {
        var keyActions = SettingsStore.GetKeyActions();
        foreach (var box in new[] { LeftAltComboBox, RightAltComboBox, LeftWinComboBox, RightWinComboBox })
        {
            var key = (string)box.Tag;
            Select(box, keyActions[key], SettingsStore.DefaultKeyActions[key]);
        }
        Select(FullScreenComboBox,
            SettingsStore.Get(SettingsStore.FullScreenPauseKey, SettingsStore.DefaultFullScreenPause),
            SettingsStore.DefaultFullScreenPause);
        _updatingSelection = true;
        DoubleTapToggle.IsOn = SettingsStore.Get(SettingsStore.DoubleTapKey, SettingsStore.DefaultDoubleTap);
        InjectedKeysToggle.IsOn = SettingsStore.Get(SettingsStore.AcceptInjectedKeysKey,
            SettingsStore.DefaultAcceptInjectedKeys);
        _updatingSelection = false;
    }

    // Selects the item whose Tag is `value`, or `defaultValue` for unknown values.
    private void Select(ComboBox box, string value, string defaultValue)
    {
        var items = box.Items.OfType<ComboBoxItem>().ToList();
        _updatingSelection = true;
        box.SelectedItem = items.FirstOrDefault(item => (string)item.Tag == value)
            ?? items.First(item => (string)item.Tag == defaultValue);
        _updatingSelection = false;
    }

    private void ShowBuildInfo()
    {
        var version = Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
        AboutExpander.Description = string.Format(CultureInfo.CurrentCulture, _resources.GetString("AboutVersion"), version);
#if DEBUG
        const string configuration = "Debug";
#else
        const string configuration = "Release";
#endif
        BuildInfoText.Text = Commit is null ? configuration : $"{ShortCommit(Commit)} · {configuration}";
    }

    // The commit in a "<version>+<commit>" string, or null if it has none.
    private static string? CommitOf(string? version) => version?.Split('+') is [_, var sha, ..] ? sha : null;

    private static string ShortCommit(string sha) => sha[..Math.Min(7, sha.Length)];

    public void BringToFront()
    {
        if (AppWindow.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized } presenter)
        {
            presenter.Restore();
        }
        Activate();
        SetForegroundWindow(_hwnd);
    }

    // For combo boxes backed by settings.json: the box's Tag is the key, each item's Tag the value.
    private async void SettingComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection ||
            sender is not ComboBox { Tag: string key, SelectedItem: ComboBoxItem { Tag: string value } })
        {
            return;
        }
        await SaveAsync(() => SettingsStore.Set(key, value));
    }

    // For the tap keys: the box's Tag is the key's name under "keys", each item's Tag the action.
    private async void KeyComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection ||
            sender is not ComboBox { Tag: string key, SelectedItem: ComboBoxItem { Tag: string action } })
        {
            return;
        }
        await SaveAsync(() => SettingsStore.SetKeyAction(key, action));
    }

    // For toggle switches backed by settings.json: the switch's Tag is the key.
    private async void SettingToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_updatingSelection || sender is not ToggleSwitch { Tag: string key, IsOn: var isOn })
        {
            return;
        }
        await SaveAsync(() => SettingsStore.Set(key, isOn));
    }

    private async Task SaveAsync(Action save)
    {
        try
        {
            save();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            await ShowErrorAsync("SaveErrorTitle", ex.Message);
        }
    }

    private async void StartupComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection)
        {
            return;
        }
        var mode = (StartupMode)StartupComboBox.SelectedIndex;
        StartupComboBox.IsEnabled = false;
        try
        {
            await StartupManager.SetModeAsync(mode);
            _startupMode = mode;
            UpdateResidentInfoBars();
        }
        catch (Win32Exception ex) when (ex.NativeErrorCode == ErrorCancelled)
        {
            RevertStartupSelection();
        }
        catch (Exception ex)
        {
            RevertStartupSelection();
            await ShowErrorAsync("StartupErrorTitle", ex.Message);
        }
        finally
        {
            StartupComboBox.IsEnabled = true;
        }
    }

    private bool ShouldRunElevated => _startupMode == StartupMode.Elevated;

    private void UpdateResidentInfoBars()
    {
        var elevated = _resident.IsElevated;
        NotRunningInfoBar.IsOpen = elevated is null;
        RestartInfoBar.IsOpen = elevated is not null && elevated != ShouldRunElevated;
        RestartInfoBar.Title = _resources.GetString(ShouldRunElevated ? "RestartElevatedTitle" : "RestartNormalTitle");
        RestartInfoBar.Message = _resources.GetString("RestartMessage");

        // A resident from before it reported its commit has none, which counts as different too.
        BuildMismatchInfoBar.IsOpen = Commit is not null && _resident.Version is not null &&
            !string.Equals(CommitOf(_resident.Version), Commit, StringComparison.OrdinalIgnoreCase);
    }

    private async void StartButton_Click(object sender, RoutedEventArgs e)
    {
        await RunResidentActionAsync(StartButton, "StartErrorTitle", () => ResidentProcess.StartAsync(ShouldRunElevated));
    }

    // Also restarts into the build next to this app, when the running one differs.
    private async void RestartButton_Click(object sender, RoutedEventArgs e)
    {
        await RunResidentActionAsync((Button)sender, "RestartErrorTitle", () => ResidentProcess.RestartAsync(ShouldRunElevated));
    }

    private async Task RunResidentActionAsync(Button button, string errorTitleKey, Func<Task> action)
    {
        button.IsEnabled = false;
        try
        {
            using (_resident.Pause())
            {
                await action();
            }
        }
        catch (Exception ex)
        {
            await ShowErrorAsync(errorTitleKey, ex.Message);
        }
        finally
        {
            button.IsEnabled = true;
            UpdateResidentInfoBars();
        }
    }

    private async void OpenSettingsFileButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            SettingsStore.EnsureFileExists();
            Process.Start(new ProcessStartInfo(SettingsStore.FilePath) { UseShellExecute = true })?.Dispose();
        }
        catch (Exception ex)
        {
            await ShowErrorAsync("OpenSettingsFileErrorTitle", ex.Message);
        }
    }

    // Next to Outatime.exe, one level above the settings app, as in the zip.
    private async void OpenThirdPartyNoticesButton_Click(object sender, RoutedEventArgs e)
    {
        var path = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "THIRD-PARTY-NOTICES.txt"));
        try
        {
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true })?.Dispose();
        }
        catch (Exception ex)
        {
            await ShowErrorAsync("OpenThirdPartyNoticesErrorTitle", ex.Message);
        }
    }

    private void RevertStartupSelection()
    {
        _updatingSelection = true;
        StartupComboBox.SelectedIndex = (int)_startupMode;
        _updatingSelection = false;
    }

    private async Task ShowErrorAsync(string titleKey, string message)
    {
        var dialog = new ContentDialog
        {
            XamlRoot = Content.XamlRoot,
            Title = _resources.GetString(titleKey),
            Content = message,
            CloseButtonText = _resources.GetString("ErrorDialogClose"),
        };
        await dialog.ShowAsync();
    }

    [LibraryImport("user32.dll")]
    private static partial uint GetDpiForWindow(nint hwnd);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetForegroundWindow(nint hwnd);

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetPropW(nint hwnd, string name, nint data);

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint RemovePropW(nint hwnd, string name);
}
