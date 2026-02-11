# 크래시 재현 로그 수집 스크립트
# Crash reproduction log collection script
# Usage: powershell -ExecutionPolicy Bypass -File run_crash_repro.ps1

param(
    [string]$Scenario = "all"   # "graceful" | "forced" | "all"
)

$binDir   = 'E:\MyGitHub\PublicStudy\NetworkModuleTest\x64\Debug'
$logDir   = 'E:\MyGitHub\PublicStudy\NetworkModuleTest\crash_logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# Named Event 를 통한 TestServer 정상 종료 신호
# GenerateConsoleCtrlEvent 는 콘솔 없는 환경에서 불안정하므로
# TestServer가 대기하는 Named Event("TestServer_GracefulShutdown")를 Set하여 종료
$shutdownEventCode = @"
using System;
using System.Runtime.InteropServices;
public static class ShutdownSignal {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenEvent(uint dwDesiredAccess, bool bInheritHandle, string lpName);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetEvent(IntPtr hEvent);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);

    // EVENT_MODIFY_STATE = 0x0002
    public static bool Send() {
        IntPtr h = OpenEvent(0x0002, false, "TestServer_GracefulShutdown");
        if (h == IntPtr.Zero) return false;
        bool ok = SetEvent(h);
        CloseHandle(h);
        return ok;
    }
}
"@
Add-Type -TypeDefinition $shutdownEventCode -Language CSharp -ErrorAction SilentlyContinue

function Run-Scenario {
    param([string]$Name, [bool]$ForceKill)

    Write-Host ""
    Write-Host "============================================" -ForegroundColor Yellow
    Write-Host "  SCENARIO: $Name" -ForegroundColor Yellow
    Write-Host "============================================" -ForegroundColor Yellow

    $dbOut  = "$logDir\${Name}_dbserver.log"
    $srvOut = "$logDir\${Name}_server.log"
    $cliOut = "$logDir\${Name}_client.log"
    $dbErr  = "$logDir\${Name}_dbserver.err"
    $srvErr = "$logDir\${Name}_server.err"
    $cliErr = "$logDir\${Name}_client.err"

    # 이전 실행의 WAL 파일 정리 (누적 방지 - forced 시나리오만 WAL 복구 테스트)
    $walFile = "$binDir\db_tasks.wal"
    if ($Name -ne "forced" -and (Test-Path $walFile)) {
        Remove-Item $walFile -Force
        Write-Host "  [*] 이전 WAL 파일 정리 완료"
    }

    # DBServer 시작
    $dbProc = Start-Process -FilePath "$binDir\TestDBServer.exe" `
        -ArgumentList '-p','8002' -WorkingDirectory $binDir -PassThru `
        -RedirectStandardOutput $dbOut -RedirectStandardError $dbErr
    Start-Sleep -Milliseconds 1200

    # TestServer 시작
    $srvProc = Start-Process -FilePath "$binDir\TestServer.exe" `
        -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' `
        -WorkingDirectory $binDir -PassThru `
        -RedirectStandardOutput $srvOut -RedirectStandardError $srvErr
    Start-Sleep -Milliseconds 1200

    # TestClient 시작
    $cliProc = Start-Process -FilePath "$binDir\TestClient.exe" `
        -ArgumentList '--host','127.0.0.1','--port','9000' `
        -WorkingDirectory $binDir -PassThru `
        -RedirectStandardOutput $cliOut -RedirectStandardError $cliErr

    # 통신 안정화 대기 (Ping/Pong 발생)
    Write-Host "  [*] 서버 안정화 대기 (5s)..."
    Start-Sleep -Seconds 5

    if ($ForceKill) {
        # TestServer만 강제종료 (크래시 시뮬레이션)
        Write-Host "  [!] TestServer 강제종료 (크래시 시뮬레이션)..." -ForegroundColor Red
        if ($srvProc -and -not $srvProc.HasExited) { $srvProc.Kill() }
        Start-Sleep -Seconds 3

        Write-Host "  [*] 크래시 후 DBServer 상태 관찰 (3s)..."
        Start-Sleep -Seconds 3
    } else {
        # Named Event 로 TestServer 정상 종료 신호 전달
        # TestServer 메인 루프가 "TestServer_GracefulShutdown" Named Event를 대기 중
        Write-Host "  [*] TestServer 정상 종료 (Named Event via SetEvent)..." -ForegroundColor Green
        $ok = [ShutdownSignal]::Send()
        Write-Host "  [*] Shutdown event sent: $ok - server.Stop() 완료 대기 (12s)..."
        # server.Stop() 이 최대 12s 안에 완료됨 (DBTaskQueue drain + WSACleanup)
        $waited = 0
        while (-not $srvProc.HasExited -and $waited -lt 120) {
            Start-Sleep -Milliseconds 100
            $waited++
        }
        if (-not $srvProc.HasExited) {
            Write-Host "  [WARN] server.Stop() 시간 초과 - 강제 종료" -ForegroundColor Red
            $srvProc.Kill()
        }
        Start-Sleep -Milliseconds 500
    }

    # 나머지 프로세스 종료
    foreach ($p in @($cliProc, $dbProc)) {
        if ($p -and -not $p.HasExited) { try { $p.Kill() } catch {} }
    }
    Start-Sleep -Milliseconds 800

    # 로그 출력
    Write-Host ""
    Write-Host "--- DBServer Log ($Name) ---" -ForegroundColor Cyan
    Get-Content $dbOut -ErrorAction SilentlyContinue
    $dbErrContent = Get-Content $dbErr -ErrorAction SilentlyContinue
    if ($dbErrContent) {
        Write-Host "[STDERR]" -ForegroundColor Red
        $dbErrContent
    }

    Write-Host ""
    Write-Host "--- Server Log ($Name) ---" -ForegroundColor Cyan
    Get-Content $srvOut -ErrorAction SilentlyContinue
    $srvErrContent = Get-Content $srvErr -ErrorAction SilentlyContinue
    if ($srvErrContent) {
        Write-Host "[STDERR]" -ForegroundColor Red
        $srvErrContent
    }

    Write-Host ""
    Write-Host "--- Client Log ($Name) ---" -ForegroundColor Cyan
    Get-Content $cliOut -ErrorAction SilentlyContinue

    Write-Host ""
    Write-Host "  [DONE] 로그 저장됨: $logDir\${Name}_*.log" -ForegroundColor Green
}

if ($Scenario -eq "graceful" -or $Scenario -eq "all") {
    Run-Scenario -Name "graceful" -ForceKill $false
    Start-Sleep -Seconds 2
}
if ($Scenario -eq "forced" -or $Scenario -eq "all") {
    Run-Scenario -Name "forced" -ForceKill $true
}

Write-Host ""
Write-Host "============================================" -ForegroundColor Yellow
Write-Host "  모든 시나리오 완료. 로그: $logDir" -ForegroundColor Yellow
Write-Host "============================================" -ForegroundColor Yellow
