# 스크립트 목적: TestDBServer -> TestServer 순서로 기동합니다.
# 포트 정책: 기본 포트가 사용 중이면 자동으로 다음 빈 포트로 이동합니다.
# 고정 포트: -DisablePortFallback 사용 시 입력한 포트를 그대로 사용합니다.
# 예시 실행: .\\run_allServer.ps1 -Configuration Debug -Platform x64

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

    throw "포트를 찾지 못했습니다. 기준 포트: $PreferredPort"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"
$serverExe = Join-Path $binDir "TestServer.exe"

if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "실행 파일을 찾을 수 없습니다: $dbExe"
    exit 1
}

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "실행 파일을 찾을 수 없습니다: $serverExe"
    exit 1
}

$resolvedDbPort = $DbPort
$resolvedServerPort = $ServerPort
if (-not $DisablePortFallback) {
    $resolvedDbPort = Resolve-AvailablePort -PreferredPort $DbPort
    $resolvedServerPort = Resolve-AvailablePort -PreferredPort $ServerPort -ReservedPorts @($resolvedDbPort)
}

if ($resolvedDbPort -ne $DbPort) {
    Write-Warning "DB 포트 $DbPort 가 이미 사용 중입니다. $resolvedDbPort 로 자동 변경합니다."
}

if ($resolvedServerPort -ne $ServerPort) {
    Write-Warning "서버 포트 $ServerPort 가 이미 사용 중입니다. $resolvedServerPort 로 자동 변경합니다."
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

Write-Host "[DB] 기동 완료: PID=$($dbProc.Id), Port=$resolvedDbPort"
Write-Host "[Server] 기동 완료: PID=$($serverProc.Id), Port=$resolvedServerPort, DB=$DbHost`:$resolvedDbPort"
Write-Host "[Client 연결 예시] .\run_client.ps1 -Configuration $Configuration -Platform $Platform -ServerPort $resolvedServerPort"
