# Validates the built SEAM VST3 with pluginval (Windows).
# Usage: scripts\run-pluginval.ps1 [-Strictness 10] [-Config Release] [-Pluginval path\to\pluginval.exe]
param(
    [int]$Strictness = 10,
    [string]$Config = "Release",
    [string]$Pluginval = $env:PLUGINVAL
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Pluginval) {
    $onPath = Get-Command pluginval -ErrorAction SilentlyContinue
    if ($onPath) { $Pluginval = $onPath.Source }
    elseif (Test-Path "C:\dev\tools\pluginval\pluginval.exe") { $Pluginval = "C:\dev\tools\pluginval\pluginval.exe" }
    else { Write-Error "pluginval not found: set PLUGINVAL or pass -Pluginval"; exit 2 }
}
$plugin = Join-Path $root "build\seam\plugin\SEAM_artefacts\$Config\VST3\SEAM.vst3"
if (-not (Test-Path $plugin)) { Write-Error "Not built: $plugin"; exit 2 }

$logs = Join-Path $root "pluginval-logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
Write-Host "pluginval: $plugin at strictness $Strictness"
# pluginval.exe is a GUI-subsystem app, so run it with Start-Process -Wait to get its exit code.
$proc = Start-Process -FilePath $Pluginval -ArgumentList @("--strictness-level", $Strictness, "--validate-in-process",
    "--output-dir", "`"$logs`"", "--timeout-ms", "600000", "--validate", "`"$plugin`"") -Wait -PassThru -NoNewWindow
if ($proc.ExitCode -eq 0) { Write-Host "pluginval PASSED at strictness $Strictness" -ForegroundColor Green }
else { Write-Host "pluginval FAILED (exit $($proc.ExitCode)); logs in $logs" -ForegroundColor Red }
exit $proc.ExitCode
