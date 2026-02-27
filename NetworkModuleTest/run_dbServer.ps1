param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$DbPort = 8002,
    [switch]$NoNewWindow
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"

if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "Executable not found: $dbExe"
    exit 1
}

Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Write-Host "Started TestDBServer on port $DbPort"
