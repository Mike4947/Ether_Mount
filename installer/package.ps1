# EtherMount Installer Package Script
# Copies the built EtherMount app and all dependencies to installer/payload/
# Run this AFTER building EtherMount (cmake --build build --config Release)
# Then compile EtherMount.iss with Inno Setup to create the installer exe.

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ReleaseDir = Join-Path $ProjectRoot "build\Release"
$PayloadDir = Join-Path $PSScriptRoot "payload"

if (-not (Test-Path $ReleaseDir)) {
    Write-Error "Release folder not found: $ReleaseDir`nBuild EtherMount first: cmake --build build --config Release"
}

# Create payload dir, clear if exists
if (Test-Path $PayloadDir) {
    Remove-Item -Path $PayloadDir -Recurse -Force
}
New-Item -ItemType Directory -Path $PayloadDir | Out-Null

# Copy everything from Release (exe, DLLs, plugins)
Copy-Item -Path "$ReleaseDir\*" -Destination $PayloadDir -Recurse -Force

Write-Host "Packaged to: $PayloadDir"
Get-ChildItem $PayloadDir -Recurse | Measure-Object | ForEach-Object { Write-Host "  $($_.Count) files" }
