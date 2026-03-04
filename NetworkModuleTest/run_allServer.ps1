# ==============================================================================
# run_allServer.ps1
# 역할: TestDBServer 와 TestServer 를 올바른 순서로 한 번에 실행한다.
#       DBServer 를 먼저 기동하고 700ms 대기 후 TestServer 를 실행하여
#       서버 간 연결이 정상적으로 수립되도록 한다.
#
# 사용법:
#   .\run_allServer.ps1 [-Configuration Debug|Release] [-Platform x64]
#                       [-ServerPort 9000] [-DbPort 8002] [-DbHost 127.0.0.1]
#                       [-NoNewWindow]
#
# 매개변수:
#   -Configuration : 빌드 구성 (기본값: Debug)
#   -Platform      : 빌드 플랫폼 (기본값: x64)
#   -ServerPort    : TestServer 클라이언트 수신 포트 (기본값: 9000)
#   -DbPort        : TestDBServer 수신 포트 (기본값: 8002)
#   -DbHost        : TestServer 가 접속할 DBServer 주소 (기본값: 127.0.0.1)
#   -NoNewWindow   : 지정 시 현재 콘솔 창에서 실행 (별도 창 미생성)
# ==============================================================================
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [int]$ServerPort = 9000,
    [int]$DbPort = 8002,
    [string]$DbHost = "127.0.0.1",
    [switch]$NoNewWindow
)

# 스크립트 위치 기준으로 실행 파일 경로 계산
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$dbExe = Join-Path $binDir "TestDBServer.exe"
$serverExe = Join-Path $binDir "TestServer.exe"

# 실행 파일 존재 여부 확인
if (-not (Test-Path -Path $dbExe -PathType Leaf)) {
    Write-Error "Executable not found: $dbExe"
    exit 1
}

if (-not (Test-Path -Path $serverExe -PathType Leaf)) {
    Write-Error "Executable not found: $serverExe"
    exit 1
}

# 1단계: TestDBServer 먼저 실행
Start-Process -FilePath $dbExe `
    -ArgumentList @("-p", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

# DBServer 가 준비될 때까지 700ms 대기
Start-Sleep -Milliseconds 700

# 2단계: TestServer 실행 (DBServer 연결 정보 전달)
Start-Process -FilePath $serverExe `
    -ArgumentList @("-p", $ServerPort, "--db-host", $DbHost, "--db-port", $DbPort) `
    -WorkingDirectory $binDir `
    -NoNewWindow:$NoNewWindow | Out-Null

Write-Host "All servers started (Server: $ServerPort, DB: $DbPort)"
