# Builds Release binaries and packs them into dist\Outatime-<version>-win-x64.zip.
param(
    [string]$Version = (git rev-parse --short HEAD)
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

function Invoke-Native {
    & $args[0] $args[1..($args.Length - 1)]
    if ($LASTEXITCODE -ne 0) {
        throw "$($args[0]) exited with code $LASTEXITCODE"
    }
}

# Native AOT's linker setup calls vswhere.exe, which is not on PATH by default.
$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$env:Path = "$env:Path;$installer"
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = & "$installer\vswhere.exe" -latest -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'Outatime'
Remove-Item -Recurse -Force $dist -ErrorAction SilentlyContinue

Invoke-Native $cmake --preset default
Invoke-Native $cmake --build --preset release
Invoke-Native dotnet publish src/Outatime.Settings -c Release -o (Join-Path $stage 'settings')

Copy-Item build/bin/Release/Outatime.exe, LICENSE $stage
Get-ChildItem $stage -Recurse -Filter *.pdb | Remove-Item
# The self-contained Windows App SDK runtime ships machine learning DLLs (~40 MB) that the settings app never loads.
Get-ChildItem (Join-Path $stage 'settings') -Include onnxruntime.dll, DirectML.dll -Recurse | Remove-Item

$zip = Join-Path $dist "Outatime-$Version-win-x64.zip"
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"
