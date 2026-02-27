param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$TargetHost = "127.0.0.1",
    [int]$ServerPort = 9000,
    [int]$ClientCount = 1,
    [switch]$NoNewWindow
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$clientExe = Join-Path $binDir "TestClient.exe"

if (-not (Test-Path -Path $clientExe -PathType Leaf)) {
    Write-Error "Executable not found: $clientExe"
    exit 1
}

if ($ClientCount -lt 1) {
    Write-Error "ClientCount must be >= 1"
    exit 1
}

Write-Host "Starting $ClientCount client(s) to $TargetHost`:$ServerPort"

for ($i = 1; $i -le $ClientCount; $i++) {
    Start-Process -FilePath $clientExe `
        -ArgumentList @("--host", $TargetHost, "--port", $ServerPort) `
        -WorkingDirectory $binDir `
        -NoNewWindow:$NoNewWindow | Out-Null

    Write-Host "Started client $i"
    Start-Sleep -Milliseconds 300
}
