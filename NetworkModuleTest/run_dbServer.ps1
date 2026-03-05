# 스크립트 목적: TestDBServer를 기동합니다.
# 포트 정책: DB 포트 충돌 시 자동으로 다음 빈 포트로 이동합니다.
# 고정 포트: -DisablePortFallback 사용 시 입력 포트를 강제합니다.
# 예시 실행: .\\run_dbServer.ps1 -DbPort 18002

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

    throw "포트를 찾지 못했습니다. 기준 포트: $PreferredPort"
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"

if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "실행 파일을 찾을 수 없습니다: $dbExe"
    exit 1
}

$resolvedDbPort = $DbPort
if (-not $DisablePortFallback) {
    $resolvedDbPort = Resolve-AvailablePort -PreferredPort $DbPort
}

if ($resolvedDbPort -ne $DbPort) {
    Write-Warning "DB 포트 $DbPort 가 이미 사용 중입니다. $resolvedDbPort 로 자동 변경합니다."
}

$dbProc = Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $resolvedDbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow `
    -PassThru

Write-Host "[DB] 기동 완료: PID=$($dbProc.Id), Port=$resolvedDbPort"
if ($DisablePortFallback) {
    Write-Host "[안내] 포트 fallback이 비활성화되어 있습니다. 기동 실패 시 -DbPort 값을 변경하세요."
}
