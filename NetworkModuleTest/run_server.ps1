param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 9000,
    [string]$DbHost = "127.0.0.1",
    [int]$DbPort = 8002,
    [switch]$NoNewWindow
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$serverExe = Join-Path $binDir "TestServer.exe"

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "Executable not found: $serverExe"
    exit 1
}

Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $ServerPort, "--db-host", $DbHost, "--db-port", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Write-Host "Started TestServer on port $ServerPort (DB: $DbHost`:$DbPort)"
