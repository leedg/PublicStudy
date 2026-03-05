# 실행 요약: TestDBServer를 실행하며 포트 충돌 시 자동 fallback 합니다.
# 사용 팁: 기본 포트를 강제하려면 -DisablePortFallback 를 사용하세요.

param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$DbPort = 18002,
    [switch]$NoNewWindow,
    [switch]$DisablePortFallback
)

function Test-PortAvailable {
    param([int]$Port)

    try {
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
        $listener.Start()
        $listener.Stop()
        return $true
    }
    catch [System.Net.Sockets.SocketException] {
        return $false
    }
}

function Resolve-AvailablePort {
    param(
        [int]$PreferredPort,
        [int[]]$ReservedPorts = @(),
        [int]$MaxScanCount = 200
    )

    for ($offset = 0; $offset -le $MaxScanCount; $offset++) {
        $candidate = $PreferredPort + $offset
        if ($ReservedPorts -contains $candidate) {
            continue
        }

        if (Test-PortAvailable -Port $candidate) {
            return $candidate
        }
    }

    throw "No available port found near $PreferredPort"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"

if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "Executable not found: $dbExe"
    exit 1
}

$resolvedDbPort = $DbPort
if (-not $DisablePortFallback) {
    $resolvedDbPort = Resolve-AvailablePort -PreferredPort $DbPort
}

if ($resolvedDbPort -ne $DbPort) {
    Write-Warning "DB port $DbPort is already in use. Falling back to $resolvedDbPort"
}

$dbProc = Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $resolvedDbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow `
    -PassThru

Write-Host "Started TestDBServer (PID: $($dbProc.Id)) on port $resolvedDbPort"
if ($DisablePortFallback) {
    Write-Host "Port fallback disabled. If startup failed, choose a different -DbPort."
}