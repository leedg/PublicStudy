# ==============================================================================
# run_server.ps1
# 역할: TestServer.exe (게임 서버)를 실행한다.
#       빌드 결과물 경로를 자동으로 계산하고, 포트 및 DB 연결 정보를 인자로 전달한다.
#
# 사용법:
#   .\run_server.ps1 [-Configuration Debug|Release] [-Platform x64]
#                    [-ServerPort 9000] [-DbHost 127.0.0.1] [-DbPort 8002]
#                    [-NoNewWindow]
#
# 매개변수:
#   -Configuration : 빌드 구성 (기본값: Debug)
#   -Platform      : 빌드 플랫폼 (기본값: x64)
#   -ServerPort    : TestServer 가 클라이언트 연결을 수신할 포트 (기본값: 9000)
#   -DbHost        : 연결할 DBServer 의 호스트 주소 (기본값: 127.0.0.1)
#   -DbPort        : 연결할 DBServer 의 포트 (기본값: 8002)
#   -NoNewWindow   : 지정 시 현재 콘솔 창에서 실행 (별도 창 미생성)
# ==============================================================================
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 9000,
    [string]$DbHost = "127.0.0.1",
    [int]$DbPort = 8002,
    [switch]$NoNewWindow
)

# 스크립트 위치 기준으로 실행 파일 경로 계산
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$serverExe = Join-Path $binDir "TestServer.exe"

# 실행 파일 존재 여부 확인
if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "Executable not found: $serverExe"
    exit 1
}

# TestServer 실행: 리슨 포트(-p), DB 호스트/포트(--db-host, --db-port) 전달
Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $ServerPort, "--db-host", $DbHost, "--db-port", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Write-Host "Started TestServer on port $ServerPort (DB: $DbHost`:$DbPort)"
