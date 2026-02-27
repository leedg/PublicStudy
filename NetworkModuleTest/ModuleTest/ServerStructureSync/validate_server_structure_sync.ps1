# ==============================================================================
# validate_server_structure_sync.ps1
# 역할: 소스 코드와 문서(Wiki 초안)가 서로 동기화되어 있는지 검증한다.
#       run_test_auto.ps1 에서 테스트 실행 전 자동으로 호출된다.
#
# 검증 항목:
#   1. TestServer.cpp / TestServer.h 파일 존재 확인
#   2. Wiki 초안 파일(Home.md, 01~05 문서) 존재 확인
#   3. DBTaskQueue worker 수가 1인지 확인 (Initialize(1, ...))
#   4. weak_ptr<DBTaskQueue> 캡처 패턴 사용 여부 확인
#   5. Graceful Shutdown 종료 순서 확인
#      (DBTaskQueue 종료 → DBServer 연결 해제 → 네트워크 엔진 종료)
#   6. 재연결 로직의 WSAECONNREFUSED 분기 및 지수 백오프 확인
#   7. Wiki 문서에 핵심 정책 키워드 포함 여부 확인
#
# 사용법:
#   .\validate_server_structure_sync.ps1 [-RepoRoot <경로>]
#
# 매개변수:
#   -RepoRoot : 저장소 루트 경로. 생략 시 스크립트 위치 기준으로 자동 계산.
#
# 반환:
#   검증 항목 중 하나라도 실패하면 exit 1 로 종료.
# ==============================================================================
param(
    [string]$RepoRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot))
{
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Assert-Check
{
    param(
        [bool]$Condition,
        [string]$Message
    )

    if ($Condition)
    {
        Write-Host "[PASS] $Message" -ForegroundColor Green
    }
    else
    {
        Write-Host "[FAIL] $Message" -ForegroundColor Red
        $script:FailCount++
    }
}

function Get-LineNumber
{
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    for ($i = 0; $i -lt $Lines.Count; ++$i)
    {
        if ($Lines[$i] -match $Pattern)
        {
            return ($i + 1)
        }
    }

    return 0
}

$script:FailCount = 0

$testServerCppPath = Join-Path $RepoRoot "Server\TestServer\src\TestServer.cpp"
$testServerHeaderPath = Join-Path $RepoRoot "Server\TestServer\include\TestServer.h"
$wikiHomePath = Join-Path $RepoRoot "Doc\WikiDraft\ServerStructure\Home.md"
$wikiArchitecturePath = Join-Path $RepoRoot "Doc\WikiDraft\ServerStructure\01-Overall-Architecture.md"
$wikiShutdownPath = Join-Path $RepoRoot "Doc\WikiDraft\ServerStructure\04-Graceful-Shutdown.md"
$wikiReconnectPath = Join-Path $RepoRoot "Doc\WikiDraft\ServerStructure\05-Reconnect-Strategy.md"

Assert-Check (Test-Path $testServerCppPath) "TestServer.cpp exists"
Assert-Check (Test-Path $testServerHeaderPath) "TestServer.h exists"
Assert-Check (Test-Path $wikiHomePath) "Wiki Home draft exists"
Assert-Check (Test-Path $wikiArchitecturePath) "Wiki Architecture draft exists"
Assert-Check (Test-Path $wikiShutdownPath) "Wiki Graceful Shutdown draft exists"
Assert-Check (Test-Path $wikiReconnectPath) "Wiki Reconnect draft exists"

if ($script:FailCount -gt 0)
{
    exit 1
}

$cppLines = Get-Content $testServerCppPath -Encoding utf8
$headerLines = Get-Content $testServerHeaderPath -Encoding utf8
$wikiHomeLines = Get-Content $wikiHomePath -Encoding utf8
$wikiArchitectureLines = Get-Content $wikiArchitecturePath -Encoding utf8
$wikiShutdownLines = Get-Content $wikiShutdownPath -Encoding utf8
$wikiReconnectLines = Get-Content $wikiReconnectPath -Encoding utf8

# 1) DBTaskQueue worker count policy
$lineInitOneWorker = Get-LineNumber -Lines $cppLines -Pattern "mDBTaskQueue->Initialize\(1,\s*""db_tasks\.wal"""
Assert-Check ($lineInitOneWorker -gt 0) "DBTaskQueue initialized with worker count = 1"

# 2) Weak pointer injection policy
$lineWeakCapture = Get-LineNumber -Lines $cppLines -Pattern "std::weak_ptr<DBTaskQueue>\s+weakQueue\s*=\s*mDBTaskQueue;"
Assert-Check ($lineWeakCapture -gt 0) "Session factory captures weak_ptr<DBTaskQueue>"

$headerText = [string]::Join("`n", $headerLines)
Assert-Check ($headerText -match "weak_ptr") "Header comments mention weak_ptr injection"

$cppText = [string]::Join("`n", $cppLines)
Assert-Check (-not ($cppText -match "captures mDBTaskQueue by pointer")) "No outdated 'capture by pointer' comment remains"

# 3) Graceful shutdown ordering
$lineShutdownQueue = Get-LineNumber -Lines $cppLines -Pattern "mDBTaskQueue->Shutdown\(\);"
$lineDisconnectDbServer = Get-LineNumber -Lines $cppLines -Pattern "DisconnectFromDBServer\(\);"
$lineStopEngine = Get-LineNumber -Lines $cppLines -Pattern "mClientEngine->Stop\(\);"

Assert-Check ($lineShutdownQueue -gt 0) "Stop() drains DBTaskQueue"
Assert-Check ($lineDisconnectDbServer -gt 0) "Stop() calls DisconnectFromDBServer()"
Assert-Check ($lineStopEngine -gt 0) "Stop() calls mClientEngine->Stop()"
Assert-Check ($lineShutdownQueue -lt $lineDisconnectDbServer) "DBTaskQueue shutdown occurs before DB server disconnect"
Assert-Check ($lineDisconnectDbServer -lt $lineStopEngine) "DB server disconnect occurs before network engine stop"

# 4) Reconnect error classification
$lineConnRefusedIf = Get-LineNumber -Lines $cppLines -Pattern "if\s*\(lastError\s*==\s*WSAECONNREFUSED\)"
$lineConnRefusedDelay = Get-LineNumber -Lines $cppLines -Pattern "delayMs\s*=\s*kConnRefusedDelayMs;"
$lineExponentialDelay = Get-LineNumber -Lines $cppLines -Pattern "delayMs\s*=\s*std::min\(delayMs\s*\*\s*2,\s*kMaxDelayMs\);"

Assert-Check ($lineConnRefusedIf -gt 0) "Reconnect loop distinguishes WSAECONNREFUSED"
Assert-Check ($lineConnRefusedDelay -gt 0) "WSAECONNREFUSED path uses fixed 1s delay"
Assert-Check ($lineExponentialDelay -gt 0) "Non-CONNREFUSED path uses exponential backoff"
Assert-Check ($lineConnRefusedIf -lt $lineConnRefusedDelay) "CONNREFUSED delay assignment appears in correct branch"
Assert-Check ($lineConnRefusedDelay -lt $lineExponentialDelay) "Fixed-delay branch appears before exponential fallback"

# 5) Wiki draft synchronization checks
$wikiHomeText = [string]::Join("`n", $wikiHomeLines)
$wikiArchitectureText = [string]::Join("`n", $wikiArchitectureLines)
$wikiShutdownText = [string]::Join("`n", $wikiShutdownLines)
$wikiReconnectText = [string]::Join("`n", $wikiReconnectLines)

Assert-Check ($wikiHomeText -match "DBTaskQueue") "Wiki Home mentions DBTaskQueue policy"
Assert-Check ($wikiArchitectureText -match "worker=1") "Wiki Architecture page shows worker=1 in structure"
Assert-Check ($wikiShutdownText -match "DBTaskQueue") "Wiki Shutdown page includes DBTaskQueue ordering"
Assert-Check ($wikiShutdownText -match "DisconnectFromDBServer") "Wiki Shutdown page includes DB disconnect step"
Assert-Check ($wikiReconnectText -match "WSAECONNREFUSED") "Wiki Reconnect page includes CONNREFUSED rule"
Assert-Check ($wikiReconnectText -match "max 30s") "Wiki Reconnect page includes exponential backoff cap"

if ($script:FailCount -gt 0)
{
    Write-Host ""
    Write-Host "Validation failed: $script:FailCount check(s)." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "All server structure sync checks passed." -ForegroundColor Cyan
