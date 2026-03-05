# 실행 요약: TestDBServer -> TestServer 순서로 기동하고, 포트 충돌 시 자동으로 빈 포트로 이동합니다.
# 사용 팁: 정확한 포트를 강제하려면 -DisablePortFallback 스위치를 사용하세요.

param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 19010,
    [int]$DbPort = 18002,
    [string]$DbHost = "127.0.0.1",
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
$serverExe = Join-Path $binDir "TestServer.exe"

if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "Executable not found: $dbExe"
    exit 1
}

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "Executable not found: $serverExe"
    exit 1
}

$resolvedDbPort = $DbPort
$resolvedServerPort = $ServerPort
if (-not $DisablePortFallback) {
    $resolvedDbPort = Resolve-AvailablePort -PreferredPort $DbPort
    $resolvedServerPort = Resolve-AvailablePort -PreferredPort $ServerPort -ReservedPorts @($resolvedDbPort)
}

if ($resolvedDbPort -ne $DbPort) {
    Write-Warning "DB port $DbPort is already in use. Falling back to $resolvedDbPort"
}

if ($resolvedServerPort -ne $ServerPort) {
    Write-Warning "Server port $ServerPort is already in use. Falling back to $resolvedServerPort"
}

$dbProc = Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $resolvedDbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow `
    -PassThru

Start-Sleep -Milliseconds 700

$serverProc = Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $resolvedServerPort, "--db-host", $DbHost, "--db-port", $resolvedDbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow `
    -PassThru

Write-Host "Started TestDBServer (PID: $($dbProc.Id)) on port $resolvedDbPort"
Write-Host "Started TestServer (PID: $($serverProc.Id)) on port $resolvedServerPort (DB: $DbHost`:$resolvedDbPort)"
Write-Host "Client command: .\run_client.ps1 -Configuration $Configuration -Platform $Platform -ServerPort $resolvedServerPort"