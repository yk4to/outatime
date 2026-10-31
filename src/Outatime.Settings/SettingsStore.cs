using System.Text.Json;
using System.Text.Json.Nodes;
using Windows.Storage;

namespace Outatime.Settings;

// Reads and writes settings.json, which the resident process watches.
internal static class SettingsStore
{
    // Keys, values and defaults must match settings.hpp/.cpp in the resident process.
    public const string KeysKey = "keys";
    public const string FullScreenPauseKey = "pauseInFullScreen";
    public const string DefaultFullScreenPause = "games";
    public const string DoubleTapKey = "doubleTap";
    public const bool DefaultDoubleTap = true;
    public const string AcceptInjectedKeysKey = "acceptInjectedKeys";
    public const bool DefaultAcceptInjectedKeys = false;

    // The tap keys under "keys" and what each does by default.
    public static readonly IReadOnlyDictionary<string, string> DefaultKeyActions = new Dictionary<string, string>
    {
        ["leftAlt"] = "toggle",
        ["rightAlt"] = "none",
        ["leftWin"] = "none",
        ["rightWin"] = "none",
    };

    private static readonly string[] KeyActions = ["none", "toggle", "on", "off"];

    // Must match settings_file.cpp in the resident process. In the package, new files under
    // %APPDATA% go to a private copy that editors outside the package can't see.
    private static readonly string SettingsDirectory = AppPackage.IsPackaged
        ? ApplicationData.Current.LocalFolder.Path
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Outatime");

    public static readonly string FilePath = Path.Combine(SettingsDirectory, "settings.json");

    // Writes the current settings if there is no file yet, so it can be opened in an editor.
    public static void EnsureFileExists()
    {
        if (!File.Exists(FilePath))
        {
            SetKeyActions(GetKeyActions());
        }
    }

    public static string Get(string key, string defaultValue) =>
        TryGet(key, out string? value) && value is not null ? value : defaultValue;

    public static bool Get(string key, bool defaultValue) =>
        TryGet(key, out bool value) ? value : defaultValue;

    public static void Set(string key, string value) => Update(json => json[key] = value);

    public static void Set(string key, bool value) => Update(json => json[key] = value);

    // What each tap key does, read the same way as the resident process: missing or
    // unknown actions keep their defaults.
    public static Dictionary<string, string> GetKeyActions()
    {
        var actions = new Dictionary<string, string>(DefaultKeyActions);
        JsonObject json;
        try
        {
            json = Load();
        }
        catch (Exception ex) when (ex is IOException or JsonException)
        {
            return actions;
        }

        if (json[KeysKey] is JsonObject keys)
        {
            foreach (var name in DefaultKeyActions.Keys)
            {
                if (keys[name] is JsonValue node && node.TryGetValue(out string? action) && KeyActions.Contains(action))
                {
                    actions[name] = action;
                }
            }
        }
        return actions;
    }

    // Changes what one tap key does. Writes all of them.
    public static void SetKeyAction(string key, string action)
    {
        var actions = GetKeyActions();
        actions[key] = action;
        SetKeyActions(actions);
    }

    private static void SetKeyActions(Dictionary<string, string> actions)
    {
        var keys = new JsonObject();
        foreach (var name in DefaultKeyActions.Keys)
        {
            keys[name] = actions[name];
        }
        Update(json => json[KeysKey] = keys);
    }

    // False if the key is missing, has another type, or the file can't be read.
    private static bool TryGet<T>(string key, out T? value)
    {
        try
        {
            if (Load()[key] is JsonValue node && node.TryGetValue(out value))
            {
                return true;
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException)
        {
        }
        value = default;
        return false;
    }

    private static void Update(Action<JsonObject> change)
    {
        JsonObject json;
        try
        {
            json = Load();
        }
        catch (JsonException)
        {
            json = [];
        }
        // Keep keys we don't know about.
        change(json);

        Directory.CreateDirectory(SettingsDirectory);
        // Write to a temporary file and swap it in, so the resident process never reads a partial file.
        var tempPath = FilePath + ".tmp";
        File.WriteAllText(tempPath, json.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        File.Move(tempPath, FilePath, overwrite: true);
    }

    private static JsonObject Load()
    {
        if (!File.Exists(FilePath))
        {
            return [];
        }
        return JsonNode.Parse(File.ReadAllText(FilePath)) as JsonObject ?? [];
    }
}
