# ==============================================================================
# run_test_2clients.ps1
# 역할: DBServer + TestServer + TestClient 2개를 자동으로 기동하고,
#       지정한 시간 동안 실행 후 정상 종료(Graceful Shutdown)시키는 통합 테스트 스크립트.
#
# 실행 순서:
#   1. 잔류 프로세스 강제 정리
#   2. TestDBServer 기동 (포트 8002)
#   3. TestServer 기동 (포트 9000, DB 연결: 127.0.0.1:8002)
#   4. TestClient #1 기동
#   5. TestClient #2 기동
#   6. $RunSeconds 초 대기
#   7. Windows Named Event 로 각 프로세스에 Graceful Shutdown 신호 전송
#      (타임아웃 시 Kill() 폴백)
#   8. 각 프로세스의 표준 출력/오류 및 종료 코드 출력
#
# 매개변수:
#   -RunSeconds : 테스트 실행 시간(초), 기본값 10
#
# 출력 로그 경로 (테스트 실행 중 %TEMP% 에 생성):
#   dbserver_out.txt / dbserver_err.txt
#   server_out.txt   / server_err.txt
#   client1_out.txt  / client1_err.txt
#   client2_out.txt  / client2_err.txt
# ==============================================================================
param([int]$RunSeconds = 10)

$binDir  = 'C:\MyGithub\PublicStudy\NetworkModuleTest\x64\Debug'
$dbOut   = "$env:TEMP\dbserver_out.txt"
$dbErr   = "$env:TEMP\dbserver_err.txt"
$srvOut  = "$env:TEMP\server_out.txt"
$srvErr  = "$env:TEMP\server_err.txt"
$cli1Out = "$env:TEMP\client1_out.txt"
$cli1Err = "$env:TEMP\client1_err.txt"
$cli2Out = "$env:TEMP\client2_out.txt"
$cli2Err = "$env:TEMP\client2_err.txt"

# English: Helper — signal a Named Event for graceful shutdown, then wait.
#          Falls back to Kill() if process hasn't exited within $timeoutMs.
# 한글: Named Event로 정상 종료 신호 전송 후 대기.
#       $timeoutMs 내에 종료 안 되면 Kill() 폴백.
function Stop-Gracefully {
    param(
        [System.Diagnostics.Process]$Proc,
        [string]$EventName,
        [int]$TimeoutMs = 5000
    )
    if (-not $Proc -or $Proc.HasExited) { return }

    $sig = [IntPtr]::Zero
    try {
        # OpenEvent(EVENT_MODIFY_STATE=0x0002, inherit=false, name)
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class Win32Event {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenEvent(uint dwDesiredAccess, bool bInheritHandle, string lpName);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetEvent(IntPtr hEvent);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
}
"@ -ErrorAction SilentlyContinue

        $sig = [Win32Event]::OpenEvent(0x0002, $false, $EventName)
        if ($sig -ne [IntPtr]::Zero) {
            [Win32Event]::SetEvent($sig) | Out-Null
            Write-Host "  Signaled '$EventName' — waiting for graceful exit..." -ForegroundColor Gray
            $Proc.WaitForExit($TimeoutMs) | Out-Null
        }
    } catch {}
    finally {
        if ($sig -ne [IntPtr]::Zero) { [Win32Event]::CloseHandle($sig) | Out-Null }
    }

    if (-not $Proc.HasExited) {
        Write-Host "  '$EventName' timeout — forcing Kill()" -ForegroundColor Yellow
        try { $Proc.Kill() } catch {}
    } else {
        Write-Host "  '$EventName' exited cleanly (code $($Proc.ExitCode))" -ForegroundColor Green
    }
}

# 기존 잔류 프로세스 정리
Get-Process -Name TestDBServer,TestServer,TestClient -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# 1) TestDBServer 기동
Write-Host "[1] Starting TestDBServer on port 8002..." -ForegroundColor Cyan
$dbProc = Start-Process -FilePath "$binDir\TestDBServer.exe" `
    -ArgumentList '-p','8002' `
    -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $dbOut -RedirectStandardError $dbErr
Start-Sleep -Milliseconds 1000

# 2) TestServer 기동
Write-Host "[2] Starting TestServer on port 9000 (--db 127.0.0.1:8002)..." -ForegroundColor Cyan
$srvProc = Start-Process -FilePath "$binDir\TestServer.exe" `
    -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' `
    -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $srvOut -RedirectStandardError $srvErr
Start-Sleep -Milliseconds 1000

# 3) Client 1 기동
Write-Host "[3] Starting TestClient #1..." -ForegroundColor Green
$cli1Proc = Start-Process -FilePath "$binDir\TestClient.exe" `
    -ArgumentList '--host','127.0.0.1','--port','9000' `
    -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $cli1Out -RedirectStandardError $cli1Err
Start-Sleep -Milliseconds 300

# 4) Client 2 기동
Write-Host "[4] Starting TestClient #2..." -ForegroundColor Green
$cli2Proc = Start-Process -FilePath "$binDir\TestClient.exe" `
    -ArgumentList '--host','127.0.0.1','--port','9000' `
    -WorkingDirectory $binDir -PassThru `
    -RedirectStandardOutput $cli2Out -RedirectStandardError $cli2Err

Write-Host "-- Running for $RunSeconds seconds --" -ForegroundColor White
Start-Sleep -Seconds $RunSeconds

# ── 정상 종료 (Named Event → 대기 → 타임아웃 시 Kill 폴백) ──────────────
Write-Host "`n[Shutdown] Sending graceful shutdown signals..." -ForegroundColor Magenta

# 클라이언트: PID 기반 Named Event
Stop-Gracefully -Proc $cli1Proc -EventName "TestClient_GracefulShutdown_$($cli1Proc.Id)" -TimeoutMs 4000
Stop-Gracefully -Proc $cli2Proc -EventName "TestClient_GracefulShutdown_$($cli2Proc.Id)" -TimeoutMs 4000

# 서버: Named Event
Stop-Gracefully -Proc $srvProc  -EventName "TestServer_GracefulShutdown"  -TimeoutMs 6000

# DB 서버: Named Event
Stop-Gracefully -Proc $dbProc   -EventName "TestDBServer_GracefulShutdown" -TimeoutMs 6000

Start-Sleep -Milliseconds 500

# ─────────── 출력 ───────────
Write-Host "`n=== DBServer Output (last 30 lines) ===" -ForegroundColor Cyan
Get-Content $dbOut  -ErrorAction SilentlyContinue | Select-Object -Last 30
Write-Host "`n=== DBServer STDERR ===" -ForegroundColor Yellow
Get-Content $dbErr  -ErrorAction SilentlyContinue | Select-Object -Last 10

Write-Host "`n=== Server Output (last 50 lines) ===" -ForegroundColor Cyan
Get-Content $srvOut -ErrorAction SilentlyContinue | Select-Object -Last 50
Write-Host "`n=== Server STDERR ===" -ForegroundColor Yellow
Get-Content $srvErr -ErrorAction SilentlyContinue | Select-Object -Last 10

Write-Host "`n=== Client #1 Output (last 25 lines) ===" -ForegroundColor Green
Get-Content $cli1Out -ErrorAction SilentlyContinue | Select-Object -Last 25
Write-Host "`n=== Client #1 STDERR ===" -ForegroundColor Yellow
Get-Content $cli1Err -ErrorAction SilentlyContinue | Select-Object -Last 5

Write-Host "`n=== Client #2 Output (last 25 lines) ===" -ForegroundColor Green
Get-Content $cli2Out -ErrorAction SilentlyContinue | Select-Object -Last 25
Write-Host "`n=== Client #2 STDERR ===" -ForegroundColor Yellow
Get-Content $cli2Err -ErrorAction SilentlyContinue | Select-Object -Last 5

# ─────────── 종료 코드 확인 ───────────
Write-Host "`n=== Process Exit Codes ===" -ForegroundColor Magenta
Write-Host "DBServer  exit: $($dbProc.ExitCode)"
Write-Host "Server    exit: $($srvProc.ExitCode)"
Write-Host "Client #1 exit: $($cli1Proc.ExitCode)"
Write-Host "Client #2 exit: $($cli2Proc.ExitCode)"
