# ==============================================================================
# run_client.ps1
# 역할: TestClient.exe (부하 테스트 클라이언트)를 1개 이상 실행한다.
#       -ClientCount 를 지정하면 지정한 수만큼 클라이언트를 300ms 간격으로 순차 실행한다.
#
# 사용법:
#   .\run_client.ps1 [-Configuration Debug|Release] [-Platform x64]
#                    [-TargetHost 127.0.0.1] [-ServerPort 9000]
#                    [-ClientCount 1] [-NoNewWindow]
#
# 매개변수:
#   -Configuration : 빌드 구성 (기본값: Debug)
#   -Platform      : 빌드 플랫폼 (기본값: x64)
#   -TargetHost    : 접속할 서버 호스트 (기본값: 127.0.0.1)
#   -ServerPort    : 접속할 서버 포트 (기본값: 9000)
#   -ClientCount   : 실행할 클라이언트 수, 1 이상 (기본값: 1)
#   -NoNewWindow   : 지정 시 현재 콘솔 창에서 실행 (별도 창 미생성)
# ==============================================================================
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$TargetHost = "127.0.0.1",
    [int]$ServerPort = 9000,
    [int]$ClientCount = 1,
    [switch]$NoNewWindow
)

# 스크립트 위치 기준으로 실행 파일 경로 계산
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$clientExe = Join-Path $binDir "TestClient.exe"

# 실행 파일 존재 여부 확인
if (-not (Test-Path -Path $clientExe -PathType Leaf)) {
    Write-Error "Executable not found: $clientExe"
    exit 1
}

# 클라이언트 수 유효성 검사
if ($ClientCount -lt 1) {
    Write-Error "ClientCount must be >= 1"
    exit 1
}

Write-Host "Starting $ClientCount client(s) to $TargetHost`:$ServerPort"

# 지정한 수만큼 클라이언트를 300ms 간격으로 순차 실행
for ($i = 1; $i -le $ClientCount; $i++) {
    Start-Process -FilePath $clientExe `
        -ArgumentList @("--host", $TargetHost, "--port", $ServerPort) `
        -WorkingDirectory $binDir `
        -NoNewWindow:$NoNewWindow | Out-Null

    Write-Host "Started client $i"
    Start-Sleep -Milliseconds 300
}
