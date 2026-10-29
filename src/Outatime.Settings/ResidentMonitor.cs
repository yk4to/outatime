using Microsoft.UI.Dispatching;

namespace Outatime.Settings;

// Follows whether the resident process is running, whether it is elevated and
// which build it is, since it can start, exit or crash at any time. Checking
// takes a few cheap window calls, so it polls once a second on the UI thread.
internal sealed class ResidentMonitor
{
    private readonly DispatcherQueueTimer _timer;
    private int _pauseCount;

    public ResidentMonitor(DispatcherQueue dispatcherQueue)
    {
        IsElevated = ResidentProcess.IsElevated();
        Version = ResidentProcess.GetVersion();
        _timer = dispatcherQueue.CreateTimer();
        _timer.Interval = TimeSpan.FromSeconds(1);
        _timer.Tick += (_, _) => Refresh();
        _timer.Start();
    }

    // Whether the resident runs as administrator, or null if it is not running.
    public bool? IsElevated { get; private set; }

    // The resident's "<version>+<commit>", or null if it is not running.
    public string? Version { get; private set; }

    // Raised on the UI thread when IsElevated or Version changes.
    public event EventHandler? Changed;

    public void Refresh()
    {
        if (_pauseCount > 0)
        {
            return;
        }
        var elevated = ResidentProcess.IsElevated();
        var version = ResidentProcess.GetVersion();
        if (elevated != IsElevated || version != Version)
        {
            IsElevated = elevated;
            Version = version;
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    // Holds off updates while the resident is being started or restarted, so it
    // doesn't show as stopped for a moment. Dispose the result to resume.
    public IDisposable Pause()
    {
        _pauseCount++;
        return new Resumer(this);
    }

    private sealed class Resumer(ResidentMonitor monitor) : IDisposable
    {
        private bool _disposed;

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            monitor._pauseCount--;
            monitor.Refresh();
        }
    }
}
