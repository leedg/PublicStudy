# 실행 요약: TestClient 인스턴스를 지정 개수만큼 순차 실행합니다.
# 참고: 서버가 fallback된 포트로 떠 있다면 -ServerPort 값을 동일하게 맞춰주세요.

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
    Write-Error "Executable not found: $clientExe"
    exit 1
}

if ($ClientCount -lt 1) {
    Write-Error "ClientCount must be >= 1"
    exit 1
}

Write-Host "Starting $ClientCount client(s) to $TargetHost`:$ServerPort"

for ($i = 1; $i -le $ClientCount; $i++) {
    $clientProc = Start-Process -FilePath $clientExe `
        -ArgumentList @("--host", $TargetHost, "--port", $ServerPort) `
        -WorkingDirectory $binDir `
        -NoNewWindow:$NoNewWindow `
        -PassThru

    Write-Host "Started client $i (PID: $($clientProc.Id))"
    Start-Sleep -Milliseconds 300
}