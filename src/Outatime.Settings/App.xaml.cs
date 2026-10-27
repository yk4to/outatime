using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;

namespace Outatime.Settings;

public partial class App : Application
{
    private MainWindow? _window;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        _window.Activate();
        AppInstance.GetCurrent().Activated += OnRedirectedActivation;
    }

    // Raised when a second launch is redirected here by Program.Main.
    private void OnRedirectedActivation(object? sender, AppActivationArguments args)
    {
        _window?.DispatcherQueue.TryEnqueue(() => _window.BringToFront());
    }
}
