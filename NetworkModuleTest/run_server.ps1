# 스크립트 목적: TestServer를 기동합니다.
# 포트 정책: 서버 포트 충돌 시 자동으로 다음 빈 포트로 이동합니다.
# 고정 포트: -DisablePortFallback 사용 시 입력 포트를 강제합니다.
# 예시 실행: .\\run_server.ps1 -ServerPort 19010 -DbPort 18002

param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 19010,
    [string]$DbHost = "127.0.0.1",
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

    throw "포트를 찾지 못했습니다. 기준 포트: $PreferredPort"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$serverExe = Join-Path $binDir "TestServer.exe"

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "실행 파일을 찾을 수 없습니다: $serverExe"
    exit 1
}

$resolvedServerPort = $ServerPort
if (-not $DisablePortFallback) {
    $resolvedServerPort = Resolve-AvailablePort -PreferredPort $ServerPort -ReservedPorts @($DbPort)
}

if ($resolvedServerPort -ne $ServerPort) {
    Write-Warning "서버 포트 $ServerPort 가 이미 사용 중입니다. $resolvedServerPort 로 자동 변경합니다."
}

$serverProc = Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $resolvedServerPort, "--db-host", $DbHost, "--db-port", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow `
    -PassThru

Write-Host "[Server] 기동 완료: PID=$($serverProc.Id), Port=$resolvedServerPort, DB=$DbHost`:$DbPort"
if ($resolvedServerPort -ne $ServerPort) {
    Write-Host "[안내] run_client.ps1 -ServerPort $resolvedServerPort 로 접속하세요."
}
