# ==============================================================================
# run_dbServer.ps1
# 역할: TestDBServer.exe (DB 연동 서버)를 실행한다.
#       게임 서버(TestServer)로부터 DB 작업 요청을 받아 처리하는 서버로,
#       TestServer 보다 먼저 실행되어야 한다.
#
# 사용법:
#   .\run_dbServer.ps1 [-Configuration Debug|Release] [-Platform x64]
#                      [-DbPort 8002] [-NoNewWindow]
#
# 매개변수:
#   -Configuration : 빌드 구성 (기본값: Debug)
#   -Platform      : 빌드 플랫폼 (기본값: x64)
#   -DbPort        : TestDBServer 가 수신할 포트 (기본값: 8002)
#   -NoNewWindow   : 지정 시 현재 콘솔 창에서 실행 (별도 창 미생성)
# ==============================================================================
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$DbPort = 8002,
    [switch]$NoNewWindow
)

# 스크립트 위치 기준으로 실행 파일 경로 계산
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"

# 실행 파일 존재 여부 확인
if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "Executable not found: $dbExe"
    exit 1
}

# TestDBServer 실행: 수신 포트(-p) 전달
Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Write-Host "Started TestDBServer on port $DbPort"
