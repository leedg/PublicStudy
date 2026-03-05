# 스크립트 목적: TestClient를 지정 개수만큼 순차 실행합니다.
# 접속 포트: 서버가 fallback으로 다른 포트로 떠 있으면 -ServerPort를 동일하게 맞춰주세요.
# 예시 실행: .\\run_client.ps1 -ServerPort 19010 -ClientCount 2

param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$TargetHost = "127.0.0.1",
    [int]$ServerPort = 19010,
    [int]$ClientCount = 1,
    [switch]$NoNewWindow
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $root "$Platform\$Configuration"
$clientExe = Join-Path $binDir "TestClient.exe"

if (-not (Test-Path -Path $clientExe -PathType Leaf)) {
    Write-Error "실행 파일을 찾을 수 없습니다: $clientExe"
    exit 1
}

if ($ClientCount -lt 1) {
    Write-Error "ClientCount 값은 1 이상이어야 합니다."
    exit 1
}

Write-Host "클라이언트 $ClientCount 개를 $TargetHost`:$ServerPort 로 실행합니다."

for ($i = 1; $i -le $ClientCount; $i++) {
    $clientProc = Start-Process -FilePath $clientExe `
        -ArgumentList @("--host", $TargetHost, "--port", $ServerPort) `
        -WorkingDirectory $binDir `
        -NoNewWindow:$NoNewWindow `
        -PassThru

    Write-Host "클라이언트 $i 기동 완료 (PID: $($clientProc.Id))"
    Start-Sleep -Milliseconds 300
}
