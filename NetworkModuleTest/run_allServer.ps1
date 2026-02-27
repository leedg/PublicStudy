param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 9000,
    [int]$DbPort = 8002,
    [string]$DbHost = "127.0.0.1",
    [switch]$NoNewWindow
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"
$serverExe = Join-Path $binDir "TestServer.exe"

if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "Executable not found: $dbExe"
    exit 1
}

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "Executable not found: $serverExe"
    exit 1
}

Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Start-Sleep -Milliseconds 700

Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $ServerPort, "--db-host", $DbHost, "--db-port", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Write-Host "All servers started (Server: $ServerPort, DB: $DbPort)"
