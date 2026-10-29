# Writes THIRD-PARTY-NOTICES.txt from the license and notice files of the
# third-party components that end up in the zip. The settings app's build runs
# this; the file is not checked in, since the .NET runtime's version follows
# the installed SDK.
#
# Needs a restored settings app (for obj/project.assets.json and the NuGet
# cache) and a configured CMake tree (for the C++ dependencies).
#
# Every package in project.assets.json must be listed in a component below or
# in $notShipped, so a new dependency fails the build until someone looks at it.
param(
    [Parameter(Mandatory)]
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

# C#/WinRT ships no license file in the Windows SDK projection package.
$csWinRTLicense = @'
MIT License

Copyright (c) Microsoft Corporation.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'@

# Packages: shown with their versions. Files: "<package>/<file>" in the NuGet
# cache, or a path relative to the repository. Text: inline, for components
# without a license file.
$components = @(
    @{
        Name = 'nlohmann/json'
        Url = 'https://github.com/nlohmann/json'
        # The version is read from the URL that src/Outatime/CMakeLists.txt fetches.
        Native = 'json'
        Files = @('build/_deps/json-src/LICENSE.MIT')
    }
    @{
        Name = '.NET Runtime'
        Url = 'https://github.com/dotnet/runtime'
        # Native AOT compiles the runtime and the libraries the app uses into the exe.
        Packages = @(
            'Microsoft.NETCore.App.Runtime.NativeAOT.win-x64'
            'Microsoft.DotNet.ILCompiler'
            'runtime.win-x64.Microsoft.DotNet.ILCompiler'
        )
        Files = @(
            'Microsoft.NETCore.App.Runtime.NativeAOT.win-x64/LICENSE.TXT'
            'Microsoft.NETCore.App.Runtime.NativeAOT.win-x64/THIRD-PARTY-NOTICES.TXT'
        )
    }
    @{
        Name = '.NET Community Toolkit'
        Url = 'https://github.com/CommunityToolkit/dotnet'
        Packages = @('CommunityToolkit.Common')
        Files = @('CommunityToolkit.Common/License.md', 'CommunityToolkit.Common/ThirdPartyNotices.txt')
    }
    @{
        Name = 'Windows Community Toolkit'
        Url = 'https://github.com/CommunityToolkit/Windows'
        Packages = @(
            'CommunityToolkit.WinUI.Controls.SettingsControls'
            'CommunityToolkit.WinUI.Extensions'
            'CommunityToolkit.WinUI.Helpers'
            'CommunityToolkit.WinUI.Triggers'
        )
        Files = @('CommunityToolkit.WinUI.Controls.SettingsControls/License.md')
    }
    @{
        Name = 'Windows App SDK'
        Url = 'https://github.com/microsoft/WindowsAppSDK'
        # Self-contained: the runtime's DLLs are copied next to the settings app.
        Packages = @(
            'Microsoft.WindowsAppSDK'
            'Microsoft.WindowsAppSDK.Base'
            'Microsoft.WindowsAppSDK.Runtime'
            'Microsoft.WindowsAppSDK.Foundation'
            'Microsoft.WindowsAppSDK.InteractiveExperiences'
            'Microsoft.WindowsAppSDK.WinUI'
            'Microsoft.WindowsAppSDK.DWrite'
            'Microsoft.WindowsAppSDK.Widgets'
        )
        Files = @(
            'Microsoft.WindowsAppSDK/license.txt'
            'Microsoft.WindowsAppSDK/NOTICE.txt'
            'Microsoft.WindowsAppSDK.WinUI/NOTICE.txt'
        )
    }
    @{
        Name = 'Microsoft Edge WebView2'
        Url = 'https://aka.ms/webview'
        Packages = @('Microsoft.Web.WebView2')
        Files = @('Microsoft.Web.WebView2/LICENSE.txt', 'Microsoft.Web.WebView2/NOTICE.txt')
    }
    @{
        Name = 'Windows SDK for .NET and C#/WinRT'
        Url = 'https://github.com/microsoft/CsWinRT'
        Packages = @('Microsoft.Windows.SDK.NET.Ref')
        Text = "The Windows SDK projection is licensed under the Microsoft Software License Terms for the Windows SDK: https://aka.ms/WinSDKLicenseURL`n`nIt includes C#/WinRT (WinRT.Runtime), licensed as follows:`n`n$csWinRTLicense"
    }
)

# Used only to build the app, or excluded with ExcludeAssets in Outatime.Settings.csproj.
$notShipped = @(
    'Microsoft.NET.ILLink.Tasks'
    'Microsoft.Windows.SDK.BuildTools'
    'Microsoft.Windows.SDK.BuildTools.MSIX'
    'Microsoft.WindowsAppSDK.AI'
    'Microsoft.WindowsAppSDK.ML'
    'Microsoft.WindowsAppSDK.Search'
    'Microsoft.Windows.AI.MachineLearning'
    'System.Numerics.Tensors'
)

$assetsPath = Join-Path $root 'src/Outatime.Settings/obj/project.assets.json'
if (-not (Test-Path $assetsPath)) {
    throw "$assetsPath not found. Restore or build src/Outatime.Settings first."
}
$assets = Get-Content $assetsPath -Raw | ConvertFrom-Json
$packageRoot = @($assets.packageFolders.PSObject.Properties.Name)[0]

# Package versions: NuGet dependencies, plus the runtime and SDK packs that the
# SDK downloads for the target framework.
$versions = @{}
foreach ($library in $assets.libraries.PSObject.Properties) {
    if ($library.Value.type -eq 'package') {
        $id, $version = $library.Name -split '/'
        $versions[$id] = $version
    }
}
foreach ($framework in $assets.project.frameworks.PSObject.Properties) {
    foreach ($pack in $framework.Value.downloadDependencies) {
        $versions[$pack.name] = $pack.version -replace '^\[([^,\]]+).*$', '$1'
    }
}

$listed = @($components | ForEach-Object { $_.Packages }) + $notShipped
$unlisted = @($assets.libraries.PSObject.Properties | Where-Object { $_.Value.type -eq 'package' } |
    ForEach-Object { ($_.Name -split '/')[0] } | Where-Object { $_ -notin $listed })
if ($unlisted) {
    throw "Not listed in $($MyInvocation.MyCommand.Name): $($unlisted -join ', '). Add each to a component if it ships, or to `$notShipped."
}

function Get-NativeVersion([string]$name) {
    $cmake = Get-Content (Join-Path $root 'src/Outatime/CMakeLists.txt') -Raw
    if ($cmake -notmatch "FetchContent_Declare\(\s*$name\s+URL\s+\S+/v([0-9.]+)/") {
        throw "No version found for $name in src/Outatime/CMakeLists.txt"
    }
    return $Matches[1]
}

function Read-Text([string]$file) {
    $id, $rest = $file -split '/', 2
    if ($versions.ContainsKey($id)) {
        $path = Join-Path $packageRoot "$($id.ToLowerInvariant())/$($versions[$id].ToLowerInvariant())/$rest"
    }
    else {
        $path = Join-Path $root $file
    }
    if (-not (Test-Path $path)) {
        throw "$path not found."
    }
    return [System.IO.File]::ReadAllText($path)
}

# LF line endings, no trailing whitespace and no leading or trailing blank lines,
# so the file diffs cleanly when a dependency is updated.
function Format-Text([string]$text) {
    $lines = $text.TrimStart([char]0xFEFF) -replace "`r`n?", "`n" -split "`n" | ForEach-Object { $_.TrimEnd() }
    return ($lines -join "`n").Trim("`n")
}

$heavy = '=' * 80
$light = '-' * 80
$out = [System.Text.StringBuilder]::new()
[void]$out.Append("Third-party notices for Outatime`n`n")
[void]$out.Append("Outatime includes the following third-party software. Each section below gives`n")
[void]$out.Append("the license and notices of one component.`n`n")
foreach ($component in $components) {
    [void]$out.Append("- $($component.Name)`n")
}

foreach ($component in $components) {
    [void]$out.Append("`n$heavy`n$($component.Name)`n$($component.Url)`n")
    if ($component.Native) {
        [void]$out.Append("Version $(Get-NativeVersion $component.Native)`n")
    }
    foreach ($id in $component.Packages) {
        if (-not $versions.ContainsKey($id)) {
            throw "$id is not in $assetsPath."
        }
        [void]$out.Append("$id $($versions[$id])`n")
    }
    [void]$out.Append("$heavy`n")

    $texts = if ($component.Text) { @($component.Text) } else { $component.Files | ForEach-Object { Read-Text $_ } }
    $first = $true
    foreach ($text in $texts) {
        if (-not $first) {
            [void]$out.Append("`n$light`n")
        }
        [void]$out.Append("`n$(Format-Text $text)`n")
        $first = $false
    }
}

[System.IO.File]::WriteAllText($Output, $out.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote $Output"
