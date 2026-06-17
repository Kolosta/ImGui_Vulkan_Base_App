# rebuild-icons.ps1
# Regenerate ONLY IconData.h from the SVGs in resources/icons, without rebuilding
# the whole app. Use after adding / editing an icon SVG.
#
#   pwsh tools/rebuild-icons.ps1            # auto-detects the build dir
#   pwsh tools/rebuild-icons.ps1 -BuildDir "out/build/<preset>"
#
# Note: a normal build already regenerates IconData.h when an SVG changes (the
# generate_icon_data custom command now depends on resources/icons/*.svg). This
# script is the fast path when you only touched icons.

param(
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrEmpty($BuildDir)) {
    # Pick the most recently modified build directory under out/build.
    $root = Join-Path $repo "out/build"
    if (-not (Test-Path $root)) {
        Write-Error "No out/build directory found. Configure the project first (CMake)."
    }
    $dir = Get-ChildItem -Path $root -Directory |
           Sort-Object LastWriteTime -Descending |
           Select-Object -First 1
    if ($null -eq $dir) { Write-Error "No build directory found under out/build." }
    $BuildDir = $dir.FullName
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repo $BuildDir
}

Write-Host "Regenerating icons via build dir:" $BuildDir -ForegroundColor Cyan
& cmake --build $BuildDir --target generate_icon_data
if ($LASTEXITCODE -ne 0) { Write-Error "Icon regeneration failed (exit $LASTEXITCODE)." }
Write-Host "IconData.h regenerated." -ForegroundColor Green
