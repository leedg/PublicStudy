# ==============================================================================
# run_perf_test.ps1
# 역할: Release 바이너리로 안정성 + 퍼포먼스 테스트를 순차 실행하고
#       결과를 PERF_HISTORY.md 에 UTF-8(BOM 없음) 로 누적 기록하는 통합 테스트 스크립트.
#
# 실행 순서:
#   Phase 0: 사전 정리 + Smoke Test (Release 빌드 기동 확인, 1 client, 10s)
#   Phase 1: 안정성 테스트
#             1-A: Graceful Shutdown (2 clients, 30s)
#             1-B: Forced Shutdown + WAL Recovery
#   Phase 2: 퍼포먼스 Ramp-up (N 클라이언트 단계별)
#
# 매개변수:
#   -Phase        : 실행 Phase (0 / 1 / 2 / all), 기본 all
#   -RampClients  : Ramp-up 단계 목록, 기본 10,100,500,1000
#   -SustainSec   : 각 Ramp 단계 유지 시간(초), 기본 30
#   -BinMode      : Release | Debug, 기본 Release
#   -SkipSmoke    : Phase 0 의 Smoke Test 생략
# ==============================================================================
param(
    [string]$Phase       = "all",
    [int[]] $RampClients = @(10, 100, 500, 1000),
    [int]   $SustainSec  = 30,
    [string]$BinMode     = "Release",
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Continue"

$BinDir  = "C:\MyGithub\PublicStudy\NetworkModuleTest\x64\$BinMode"
$LogRoot = "C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs"
$RunTag  = (Get-Date -Format "yyyyMMdd_HHmmss")
$LogDir  = "$LogRoot\$RunTag"
New-Item -ItemType Directory -Path $LogDir -Force | Out-Null

# ==============================================================================
# 공통 헬퍼
# ==============================================================================

# English: Write UTF-8 (no BOM) content to file, appending
# 한글: BOM 없는 UTF-8 로 파일에 내용 추가
function Write-Log {
    param([string]$Path, [string]$Content)
    # English: Use UTF-8 without BOM to avoid inline BOM when appending
    # 한글: 추가(append) 시 인라인 BOM 발생 방지를 위해 BOM 없는 UTF-8 사용
    $enc = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::AppendAllText($Path, $Content, $enc)
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class Win32NamedEvent {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenEvent(uint dwDesiredAccess, bool bInheritHandle, string lpName);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetEvent(IntPtr hEvent);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
}
"@ -ErrorAction SilentlyContinue

function Stop-Gracefully {
    param(
        [System.Diagnostics.Process]$Proc,
        [string]$EventName,
        [int]$TimeoutMs = 5000
    )
    if (-not $Proc -or $Proc.HasExited) { return }
    $sig = [IntPtr]::Zero
    try {
        $sig = [Win32NamedEvent]::OpenEvent(0x0002, $false, $EventName)
        if ($sig -ne [IntPtr]::Zero) {
            [Win32NamedEvent]::SetEvent($sig) | Out-Null
            $Proc.WaitForExit($TimeoutMs) | Out-Null
        }
    } finally {
        if ($sig -ne [IntPtr]::Zero) { [Win32NamedEvent]::CloseHandle($sig) | Out-Null }
    }
    if (-not $Proc.HasExited) { try { $Proc.Kill() } catch {} }
}

function Kill-All {
    Get-Process -Name TestDBServer, TestServer, TestClient -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

function Get-ProcStats {
    param([System.Diagnostics.Process]$Proc)
    if (-not $Proc -or $Proc.HasExited) { return "N/A" }
    $Proc.Refresh()
    $ws  = [math]::Round($Proc.WorkingSet64 / 1MB, 1)
    $hdl = $Proc.HandleCount
    $thr = $Proc.Threads.Count
    return "WS=${ws}MB Handles=$hdl Threads=$thr"
}

function Parse-RTT {
    param([string]$LogFile)
    if (-not (Test-Path $LogFile)) { return "RTT N/A" }
    $lines = Get-Content $LogFile -ErrorAction SilentlyContinue
    # English: Client log format: "  Min RTT    : 6 ms"
    # 한글: 클라이언트 출력 형식: "  Min RTT    : 6 ms"
    $minLine  = $lines | Select-String "Min RTT"  | Select-Object -Last 1
    $avgLine  = $lines | Select-String "Avg RTT"  | Select-Object -Last 1
    $maxLine  = $lines | Select-String "Max RTT"  | Select-Object -Last 1
    $pongLine = $lines | Select-String "Pong recv" | Select-Object -Last 1
    $min  = if ($minLine)  { ($minLine  -replace ".*:\s*", "" -replace "\s*ms.*", "").Trim() } else { "N/A" }
    $avg  = if ($avgLine)  { ($avgLine  -replace ".*:\s*", "" -replace "\s*ms.*", "").Trim() } else { "N/A" }
    $max  = if ($maxLine)  { ($maxLine  -replace ".*:\s*", "" -replace "\s*ms.*", "").Trim() } else { "N/A" }
    $pong = if ($pongLine) { ($pongLine -replace ".*:\s*", "").Trim() } else { "N/A" }
    if ($avg -ne "N/A") { return "RTT min=${min}ms avg=${avg}ms max=${max}ms Pong=$pong" }
    return "RTT N/A"
}

function Count-Connected {
    param([string]$LogFile)
    if (-not (Test-Path $LogFile)) { return 0 }
    return (Get-Content $LogFile -ErrorAction SilentlyContinue | Select-String "Client connected - IP:" | Measure-Object).Count
}

function Count-Errors {
    param([string]$LogFile)
    if (-not (Test-Path $LogFile)) { return 0 }
    # English: Count [ERROR] lines only. Known expected WARNs ("DB server closed connection") are excluded.
    # 한글: [ERROR] 만 집계. 정상 종료 시 예상되는 WARN("DB server closed connection") 제외.
    return (Get-Content $LogFile -ErrorAction SilentlyContinue | Select-String "\[ERROR\]" | Measure-Object).Count
}

function Check-Log {
    param([string]$LogFile, [string]$Pattern)
    if (-not (Test-Path $LogFile)) { return $false }
    return ($null -ne (Get-Content $LogFile -ErrorAction SilentlyContinue | Select-String $Pattern))
}

# ==============================================================================
# 누적 결과 파일 초기화
# ==============================================================================
$SummaryFile = "$LogRoot\PERF_HISTORY.md"
if (-not (Test-Path $SummaryFile)) {
    $initContent = "# NetworkModuleTest 퍼포먼스 테스트 누적 기록`n`n" +
        "> 이 파일은 run_perf_test.ps1 실행 시 자동 갱신됩니다.`n" +
        "> 각 실행 결과는 섹션으로 누적됩니다. 이전 기록은 절대 수정하지 않습니다.`n`n---`n`n"
    [System.IO.File]::WriteAllText($SummaryFile, $initContent, [System.Text.Encoding]::UTF8)
}

$RunHeader = "`n---`n`n## 실행: $RunTag`n`n" +
    "- **빌드**: x64 $BinMode`n" +
    "- **시각**: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`n" +
    "- **Phase**: $Phase`n" +
    "- **Ramp 단계**: $($RampClients -join ', ') clients`n" +
    "- **각 단계 유지**: ${SustainSec}초`n`n"
Write-Log $SummaryFile $RunHeader

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  run_perf_test.ps1  [$RunTag]" -ForegroundColor Cyan
Write-Host "  BinMode=$BinMode  Phase=$Phase" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# ==============================================================================
# Phase 0: 사전 정리 + Smoke Test
# ==============================================================================
if ($Phase -eq "all" -or $Phase -eq "0") {
    Write-Host "[Phase 0] 잔류 프로세스 정리..." -ForegroundColor Yellow
    Kill-All

    if (-not $SkipSmoke) {
        Write-Host "[Phase 0] Smoke Test (Release, 1 client, 10s)..." -ForegroundColor Yellow
        $p0_db  = "$LogDir\p0_db.txt"
        $p0_srv = "$LogDir\p0_srv.txt"
        $p0_cli = "$LogDir\p0_cli.txt"

        $dbProc  = Start-Process "$BinDir\TestDBServer.exe" -ArgumentList '-p','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p0_db  -RedirectStandardError "$LogDir\p0_db_err.txt"
        Start-Sleep -Milliseconds 1200
        $srvProc = Start-Process "$BinDir\TestServer.exe"  -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p0_srv -RedirectStandardError "$LogDir\p0_srv_err.txt"
        Start-Sleep -Milliseconds 1200
        $cliProc = Start-Process "$BinDir\TestClient.exe"  -ArgumentList '--host','127.0.0.1','--port','9000' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p0_cli -RedirectStandardError "$LogDir\p0_cli_err.txt"

        Start-Sleep -Seconds 10

        $srvStats = Get-ProcStats $srvProc
        $dbStats  = Get-ProcStats $dbProc

        Stop-Gracefully $cliProc "TestClient_GracefulShutdown_$($cliProc.Id)" 4000
        Stop-Gracefully $srvProc "TestServer_GracefulShutdown" 6000
        Stop-Gracefully $dbProc  "TestDBServer_GracefulShutdown" 6000
        Start-Sleep -Milliseconds 500

        $rtt    = Parse-RTT $p0_cli
        $conns  = Count-Connected $p0_srv
        $errs   = Count-Errors $p0_srv
        # English: Judge by log content (ExitCode can be null with redirected streams in PS5)
        # 한글: ExitCode 대신 로그 내용으로 판정 (PS5 에서 리다이렉트 시 ExitCode 가 null 될 수 있음)
        $srvOk  = Check-Log $p0_srv "Server shutdown complete"
        $cliOk  = Check-Log $p0_cli "TestClient shutdown complete"
        $p0_pass  = ($errs -eq 0) -and ($conns -ge 1) -and $srvOk -and $cliOk
        $p0_judge = if ($p0_pass) { "PASS" } else { "FAIL" }
        $p0_color = if ($p0_pass) { "Green" } else { "Red" }

        Write-Host "  Smoke: $p0_judge  $rtt  Conns=$conns  Errors=$errs  SrvOk=$srvOk  CliOk=$cliOk" -ForegroundColor $p0_color

        $smokeSection = "### Phase 0 — Smoke Test (Release, 1 client, 10s)`n`n" +
            "| 항목 | 값 |`n|------|-----|`n" +
            "| 결과 | **$p0_judge** |`n" +
            "| 연결 수 | $conns |`n" +
            "| $rtt | |`n" +
            "| 서버 리소스 | $srvStats |`n" +
            "| DB 리소스 | $dbStats |`n" +
            "| [ERROR] 수 | $errs |`n" +
            "| 서버 정상 종료 | $srvOk |`n" +
            "| 클라이언트 정상 종료 | $cliOk |`n`n"
        Write-Log $SummaryFile $smokeSection

        if (-not $p0_pass) {
            Write-Host "[Phase 0] Smoke Test FAIL - 이후 단계 중단" -ForegroundColor Red
            exit 1
        }
    }
}

# ==============================================================================
# Phase 1: 안정성 테스트
# ==============================================================================
if ($Phase -eq "all" -or $Phase -eq "1") {
    Write-Host "`n[Phase 1] 안정성 테스트..." -ForegroundColor Magenta
    Write-Log $SummaryFile "### Phase 1 — 안정성 테스트`n`n"

    # 1-A: Graceful Shutdown (2 clients, 30s)
    Write-Host "  [1-A] Graceful Shutdown (2 clients, 30s)" -ForegroundColor Cyan
    Kill-All
    $p1a_db  = "$LogDir\p1a_db.txt"
    $p1a_srv = "$LogDir\p1a_srv.txt"
    $p1a_c1  = "$LogDir\p1a_c1.txt"
    $p1a_c2  = "$LogDir\p1a_c2.txt"

    $dbProc  = Start-Process "$BinDir\TestDBServer.exe" -ArgumentList '-p','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1a_db  -RedirectStandardError "$LogDir\p1a_db_err.txt"
    Start-Sleep -Milliseconds 1200
    $srvProc = Start-Process "$BinDir\TestServer.exe"  -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1a_srv -RedirectStandardError "$LogDir\p1a_srv_err.txt"
    Start-Sleep -Milliseconds 1200
    $c1Proc  = Start-Process "$BinDir\TestClient.exe"  -ArgumentList '--host','127.0.0.1','--port','9000' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1a_c1 -RedirectStandardError "$LogDir\p1a_c1_err.txt"
    Start-Sleep -Milliseconds 300
    $c2Proc  = Start-Process "$BinDir\TestClient.exe"  -ArgumentList '--host','127.0.0.1','--port','9000' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1a_c2 -RedirectStandardError "$LogDir\p1a_c2_err.txt"

    Start-Sleep -Seconds 30

    $srvStats1a = Get-ProcStats $srvProc

    Stop-Gracefully $c1Proc  "TestClient_GracefulShutdown_$($c1Proc.Id)" 4000
    Stop-Gracefully $c2Proc  "TestClient_GracefulShutdown_$($c2Proc.Id)" 4000
    Stop-Gracefully $srvProc "TestServer_GracefulShutdown" 6000
    Stop-Gracefully $dbProc  "TestDBServer_GracefulShutdown" 6000
    Start-Sleep -Milliseconds 500

    $p1a_conns = Count-Connected $p1a_srv
    $p1a_errs  = Count-Errors    $p1a_srv
    # English: DBTaskQueue shutdown log is in TestServer log, not DBServer log
    # 한글: DBTaskQueue 종료 로그는 DBServer 가 아니라 TestServer 로그에 있음
    $p1a_drain = Check-Log $p1a_srv "DBTaskQueue shutdown complete"
    $p1a_srvOk = Check-Log $p1a_srv "Server shutdown complete"
    $p1a_pass  = ($p1a_errs -eq 0) -and $p1a_srvOk -and $p1a_drain
    $p1a_judge = if ($p1a_pass) { "PASS" } else { "FAIL" }
    $p1a_drain_str = if ($p1a_drain) { "Yes" } else { "No" }
    $p1a_color = if ($p1a_pass) { "Green" } else { "Red" }
    Write-Host "    1-A Graceful: $p1a_judge  Conns=$p1a_conns  QueueDrain=$p1a_drain_str  Errors=$p1a_errs" -ForegroundColor $p1a_color

    # 1-B: Forced Shutdown + WAL 복구 확인
    Write-Host "  [1-B] Forced Shutdown + WAL Recovery" -ForegroundColor Cyan
    $p1b_db   = "$LogDir\p1b_db.txt"
    $p1b_srv  = "$LogDir\p1b_srv.txt"
    $p1b_cli  = "$LogDir\p1b_cli.txt"
    $p1b_srv2 = "$LogDir\p1b_srv2.txt"

    $dbProc  = Start-Process "$BinDir\TestDBServer.exe" -ArgumentList '-p','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1b_db  -RedirectStandardError "$LogDir\p1b_db_err.txt"
    Start-Sleep -Milliseconds 1200
    $srvProc = Start-Process "$BinDir\TestServer.exe"   -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1b_srv -RedirectStandardError "$LogDir\p1b_srv_err.txt"
    Start-Sleep -Milliseconds 1200
    $cliProc = Start-Process "$BinDir\TestClient.exe"   -ArgumentList '--host','127.0.0.1','--port','9000' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1b_cli -RedirectStandardError "$LogDir\p1b_cli_err.txt"
    Start-Sleep -Seconds 5

    # 서버 강제 Kill
    if (-not $srvProc.HasExited) { $srvProc.Kill() }
    Start-Sleep -Milliseconds 800

    # 서버 재기동 → WAL 복구 확인
    $srvProc2 = Start-Process "$BinDir\TestServer.exe" -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p1b_srv2 -RedirectStandardError "$LogDir\p1b_srv2_err.txt"
    Start-Sleep -Seconds 5

    $p1b_wal_recover = Check-Log $p1b_srv2 "WAL: Recovering"
    $p1b_wal_clean   = Check-Log $p1b_srv2 "WAL: Clean startup"
    $p1b_reconnect   = Check-Log $p1b_cli  "Reconnecting"

    Stop-Gracefully $cliProc  "TestClient_GracefulShutdown_$($cliProc.Id)" 3000
    Stop-Gracefully $srvProc2 "TestServer_GracefulShutdown" 6000
    Stop-Gracefully $dbProc   "TestDBServer_GracefulShutdown" 6000
    Start-Sleep -Milliseconds 500

    $p1b_wal_status     = if ($p1b_wal_recover) { "Recovery" } elseif ($p1b_wal_clean) { "Clean" } else { "No WAL log" }
    $p1b_reconnect_str  = if ($p1b_reconnect) { "Yes" } else { "No" }
    Write-Host "    1-B Forced+WAL: WAL=$p1b_wal_status  ClientReconnect=$p1b_reconnect_str" -ForegroundColor Cyan

    $p1_section = "#### 1-A: Graceful Shutdown (2 clients, 30s)`n`n" +
        "| 항목 | 값 |`n|------|-----|`n" +
        "| 결과 | **$p1a_judge** |`n" +
        "| 연결된 클라이언트 수 | $p1a_conns |`n" +
        "| 서버 리소스 (종료 직전) | $srvStats1a |`n" +
        "| DBTaskQueue 드레인 | $p1a_drain_str |`n" +
        "| [ERROR] 수 | $p1a_errs |`n`n" +
        "#### 1-B: Forced Shutdown + WAL Recovery`n`n" +
        "| 항목 | 값 |`n|------|-----|`n" +
        "| WAL 상태 (재기동 후) | $p1b_wal_status |`n" +
        "| 클라이언트 자동 재연결 시도 | $p1b_reconnect_str |`n`n"
    Write-Log $SummaryFile $p1_section

    Kill-All
}

# ==============================================================================
# Phase 2: 퍼포먼스 Ramp-up
# ==============================================================================
if ($Phase -eq "all" -or $Phase -eq "2") {
    Write-Host "`n[Phase 2] 퍼포먼스 Ramp-up..." -ForegroundColor Magenta

    $rampHeader = "### Phase 2 — 퍼포먼스 Ramp-up (x64 $BinMode, ${SustainSec}s/단계)`n`n" +
        "| 단계 | 목표 연결 | 실제 연결 | [ERROR] 수 | Server WS(MB) | Server Handles | RTT (클라이언트 1번) | 판정 |`n" +
        "|------|-----------|-----------|------------|---------------|----------------|----------------------|------|`n"
    Write-Log $SummaryFile $rampHeader

    foreach ($N in $RampClients) {
        Write-Host "  [Phase 2] ${N} clients (${SustainSec}s)..." -ForegroundColor Yellow
        Kill-All

        $p2_db  = "$LogDir\p2_n${N}_db.txt"
        $p2_srv = "$LogDir\p2_n${N}_srv.txt"
        $p2_c0  = "$LogDir\p2_n${N}_c0.txt"  # 첫 번째 클라이언트만 RTT 수집

        $dbProc  = Start-Process "$BinDir\TestDBServer.exe" -ArgumentList '-p','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p2_db -RedirectStandardError "$LogDir\p2_n${N}_db_err.txt"
        Start-Sleep -Milliseconds 1200
        $srvProc = Start-Process "$BinDir\TestServer.exe"   -ArgumentList '-p','9000','--db-host','127.0.0.1','--db-port','8002' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $p2_srv -RedirectStandardError "$LogDir\p2_n${N}_srv_err.txt"
        Start-Sleep -Milliseconds 1200

        # 클라이언트 N개 기동 (10개마다 100ms 간격)
        $cliProcs = @()
        for ($i = 0; $i -lt $N; $i++) {
            $cOut = if ($i -eq 0) { $p2_c0 } else { "$LogDir\p2_n${N}_c${i}.txt" }
            $p = Start-Process "$BinDir\TestClient.exe" -ArgumentList '--host','127.0.0.1','--port','9000' -WorkingDirectory $BinDir -PassThru -RedirectStandardOutput $cOut -RedirectStandardError "$LogDir\p2_n${N}_c${i}_err.txt"
            $cliProcs += $p
            if ($i % 10 -eq 9) { Start-Sleep -Milliseconds 100 }
        }

        Start-Sleep -Seconds $SustainSec

        # 중간 지표 수집
        $srvStats = Get-ProcStats $srvProc

        # Graceful shutdown
        foreach ($cp in $cliProcs) {
            Stop-Gracefully $cp "TestClient_GracefulShutdown_$($cp.Id)" 3000
        }
        Stop-Gracefully $srvProc "TestServer_GracefulShutdown" 8000
        Stop-Gracefully $dbProc  "TestDBServer_GracefulShutdown" 6000
        Start-Sleep -Milliseconds 800

        $conns = Count-Connected $p2_srv
        $errs  = Count-Errors    $p2_srv
        $rtt   = Parse-RTT $p2_c0
        $ws    = if ($srvStats -match "WS=([0-9.]+)MB") { $Matches[1] } else { "N/A" }
        $hdl   = if ($srvStats -match "Handles=([0-9]+)") { $Matches[1] } else { "N/A" }

        $pass  = ($errs -lt ($N * 2)) -and ($conns -ge [math]::Floor($N * 0.9))
        $judge = if ($pass) { "PASS" } else { "FAIL" }
        $p2_color = if ($pass) { "Green" } else { "Red" }
        Write-Host "    N=${N}: $judge  Conns=$conns/${N}  Errors=$errs  $rtt  WS=${ws}MB  Handles=$hdl" -ForegroundColor $p2_color

        $row = "| $N | $N | $conns | $errs | $ws | $hdl | $rtt | **$judge** |`n"
        Write-Log $SummaryFile $row

        # 클라이언트 로그 정리 (대표 1개만 유지)
        for ($i = 1; $i -lt $cliProcs.Count; $i++) {
            Remove-Item "$LogDir\p2_n${N}_c${i}.txt"     -ErrorAction SilentlyContinue
            Remove-Item "$LogDir\p2_n${N}_c${i}_err.txt" -ErrorAction SilentlyContinue
        }

        Kill-All
        Start-Sleep -Seconds 2
    }

    Write-Log $SummaryFile "`n"
}

# ==============================================================================
# 최종 정리
# ==============================================================================
$footer = "### 이번 실행 상세 로그 위치`n`n``$LogDir```n`n---`n"
Write-Log $SummaryFile $footer

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  완료. 누적 기록: $SummaryFile" -ForegroundColor Green
Write-Host "  상세 로그:       $LogDir" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Cyan
