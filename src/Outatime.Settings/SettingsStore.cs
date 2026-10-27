using System.Text.Json;
using System.Text.Json.Nodes;

namespace Outatime.Settings;

// Reads and writes %APPDATA%\Outatime\settings.json, which the resident process watches.
internal static class SettingsStore
{
    public const string DefaultMode = "toggle";

    private static readonly string SettingsDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Outatime");

    public static readonly string FilePath = Path.Combine(SettingsDirectory, "settings.json");

    // Writes the current settings if there is no file yet, so it can be opened in an editor.
    public static void EnsureFileExists()
    {
        if (!File.Exists(FilePath))
        {
            SaveMode(LoadMode());
        }
    }

    public static string LoadMode()
    {
        try
        {
            if (Load()["mode"] is JsonValue value && value.TryGetValue(out string? mode))
            {
                return mode;
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException)
        {
        }
        return DefaultMode;
    }

    public static void SaveMode(string mode)
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
        json["mode"] = mode;

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
