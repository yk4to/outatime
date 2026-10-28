using System.Text.Json;
using System.Text.Json.Nodes;

namespace Outatime.Settings;

// Reads and writes %APPDATA%\Outatime\settings.json, which the resident process watches.
internal static class SettingsStore
{
    // Keys, values and defaults must match settings.hpp/.cpp in the resident process.
    public const string ModeKey = "mode";
    public const string DefaultMode = "toggle";
    public const string FullScreenPauseKey = "pauseInFullScreen";
    public const string DefaultFullScreenPause = "games";
    public const string DoubleTapKey = "doubleTap";
    public const bool DefaultDoubleTap = true;

    private static readonly string SettingsDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Outatime");

    public static readonly string FilePath = Path.Combine(SettingsDirectory, "settings.json");

    // Writes the current settings if there is no file yet, so it can be opened in an editor.
    public static void EnsureFileExists()
    {
        if (!File.Exists(FilePath))
        {
            Set(ModeKey, Get(ModeKey, DefaultMode));
        }
    }

    public static string Get(string key, string defaultValue) =>
        TryGet(key, out string? value) && value is not null ? value : defaultValue;

    public static bool Get(string key, bool defaultValue) =>
        TryGet(key, out bool value) ? value : defaultValue;

    public static void Set(string key, string value) => Set(key, JsonValue.Create(value)!);

    public static void Set(string key, bool value) => Set(key, JsonValue.Create(value));

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

    private static void Set(string key, JsonNode value)
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
        json[key] = value;

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
