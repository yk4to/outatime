using System.ComponentModel;
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

    private readonly ResourceLoader _resources = new();
    private readonly nint _hwnd;

    // Set while the code changes a selection, so the change handlers ignore it.
    private bool _updatingSelection;
    private StartupMode _startupMode;

    public MainWindow()
    {
        InitializeComponent();

        Title = _resources.GetString("AppTitle");
        AppTitleText.Text = Title;
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        // AppWindow.Resize takes physical pixels, so scale by the window's DPI.
        _hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var scale = GetDpiForWindow(_hwnd) / 96.0;
        AppWindow.Resize(new SizeInt32((int)(DefaultWidth * scale), (int)(DefaultHeight * scale)));

        _startupMode = StartupManager.GetMode();
        _updatingSelection = true;
        var mode = SettingsStore.LoadMode();
        ModeComboBox.SelectedItem = ModeComboBox.Items.OfType<ComboBoxItem>()
            .FirstOrDefault(item => (string)item.Tag == mode) ?? ModeComboBox.Items[0];
        StartupComboBox.SelectedIndex = (int)_startupMode;
        _updatingSelection = false;

        var version = Assembly.GetExecutingAssembly().GetName().Version!.ToString(3);
        AboutCard.Description = string.Format(_resources.GetString("AboutVersion"), version);

        UpdateRestartInfoBar();
        // The resident may have been restarted elsewhere while this window was in the background.
        Activated += (_, args) =>
        {
            if (args.WindowActivationState != WindowActivationState.Deactivated)
            {
                UpdateRestartInfoBar();
            }
        };
    }

    public void BringToFront()
    {
        if (AppWindow.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized } presenter)
        {
            presenter.Restore();
        }
        Activate();
        SetForegroundWindow(_hwnd);
    }

    private async void ModeComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection || ModeComboBox.SelectedItem is not ComboBoxItem { Tag: string mode })
        {
            return;
        }
        try
        {
            SettingsStore.SaveMode(mode);
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
            UpdateRestartInfoBar();
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

    private void UpdateRestartInfoBar()
    {
        var elevated = ResidentProcess.IsElevated();
        RestartInfoBar.IsOpen = elevated is not null && elevated != ShouldRunElevated;
        RestartTitleRun.Text = _resources.GetString(ShouldRunElevated ? "RestartElevatedTitle" : "RestartNormalTitle");
        RestartMessageRun.Text = _resources.GetString("RestartMessage");
    }

    private async void RestartButton_Click(object sender, RoutedEventArgs e)
    {
        RestartButton.IsEnabled = false;
        try
        {
            await ResidentProcess.RestartAsync(ShouldRunElevated);
        }
        catch (Exception ex)
        {
            await ShowErrorAsync("RestartErrorTitle", ex.Message);
        }
        finally
        {
            RestartButton.IsEnabled = true;
            UpdateRestartInfoBar();
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
}
