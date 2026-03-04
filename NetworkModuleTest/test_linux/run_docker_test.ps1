# =============================================================================
# run_docker_test.ps1 — Windows host launcher for Docker Linux integration tests
# =============================================================================
# English: Builds Docker images, runs epoll/io_uring integration tests,
#          optionally commits and pushes the new log files to git.
# 한글: Docker 이미지 빌드, epoll/io_uring 통합 테스트 실행,
#       선택적으로 새 로그 파일을 git에 커밋/푸시.
#
# ─── Usage ──────────────────────────────────────────────────────────────────
#   .\test_linux\run_docker_test.ps1                   # build + test
#   .\test_linux\run_docker_test.ps1 -Push             # build + test + push results
#   .\test_linux\run_docker_test.ps1 -Backend epoll    # epoll only
#   .\test_linux\run_docker_test.ps1 -NoBuild          # skip image rebuild
#   .\test_linux\run_docker_test.ps1 -Single           # single-container quick test
#   .\test_linux\run_docker_test.ps1 -NoBuild -Push    # rerun tests then push
# ────────────────────────────────────────────────────────────────────────────

param(
    [ValidateSet("both", "epoll", "iouring")]
    [string]$Backend = "both",

    [switch]$NoBuild,

    # English: Single-container mode (no 3-tier compose, no result files)
    # 한글: 단일 컨테이너 모드 (3-tier 없음, 결과 파일 없음)
    [switch]$Single,

    # English: Commit and push new log files to git after tests complete.
    #          Requires the working tree to be in a git repository.
    # 한글: 테스트 완료 후 새 로그 파일을 git에 커밋/푸시.
    #       git 저장소 내에서 실행해야 함.
    [switch]$Push
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# English: Locate NetworkModuleTest root (parent of this script's directory)
# 한글: NetworkModuleTest 루트 찾기 (스크립트 디렉토리의 상위)
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot    = Split-Path -Parent $ScriptDir       # NetworkModuleTest/
$GitRoot     = Split-Path -Parent $RepoRoot        # PublicStudy/ (git root)
$ComposeFile = Join-Path $ScriptDir "docker-compose.yml"
$LogDir      = Join-Path $RepoRoot "Doc\Performance\Logs"

# English: Generate a shared session timestamp so epoll+iouring go in the same folder.
#          Format: YYYYMMDD_HHMMSS_linux  e.g. 20260302_153000_linux
# 한글: epoll+iouring 결과가 같은 폴더에 들어가도록 공유 세션 타임스탬프 생성.
$LogSession = (Get-Date -Format "yyyyMMdd_HHmmss") + "_linux"

Push-Location $RepoRoot

try {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  NetworkModuleTest Docker Linux Test"   -ForegroundColor Cyan
    Write-Host "  Root      : $RepoRoot"                 -ForegroundColor Cyan
    Write-Host "  Backend   : $Backend"                  -ForegroundColor Cyan
    Write-Host "  LogSession: $LogSession"               -ForegroundColor Cyan
    Write-Host "  Push      : $Push"                     -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""

    # -------------------------------------------------------------------------
    # English: Ensure log directory exists on host (required for volume mount)
    # 한글: 호스트에 로그 디렉토리 생성 (볼륨 마운트에 필요)
    # -------------------------------------------------------------------------
    if (-not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir | Out-Null
        Write-Host "[setup] Created log directory: $LogDir" -ForegroundColor DarkGray
    }

    # -------------------------------------------------------------------------
    # English: Build Docker image
    # 한글: Docker 이미지 빌드
    # -------------------------------------------------------------------------
    if (-not $NoBuild) {
        Write-Host "[step 1] Building Docker image..." -ForegroundColor Yellow
        docker-compose -f $ComposeFile build
        if ($LASTEXITCODE -ne 0) { throw "docker-compose build failed (exit $LASTEXITCODE)" }
        Write-Host "[step 1] Image build complete." -ForegroundColor Green
    } else {
        Write-Host "[step 1] Skipping image build (-NoBuild)." -ForegroundColor DarkGray
    }

    # -------------------------------------------------------------------------
    # English: Single-container mode
    # 한글: 단일 컨테이너 모드
    # -------------------------------------------------------------------------
    if ($Single) {
        Write-Host ""
        Write-Host "[step 2] Single-container integration test..." -ForegroundColor Yellow
        docker-compose -f $ComposeFile run --rm build `
            /workspace/test_linux/scripts/run_integration.sh
        if ($LASTEXITCODE -ne 0) { throw "Integration test failed (exit $LASTEXITCODE)" }
        Write-Host "[step 2] Single-container test PASSED." -ForegroundColor Green
        return
    }

    # -------------------------------------------------------------------------
    # English: 3-tier compose tests — inject LOG_SESSION so both backends share
    #          the same result directory (e.g. 20260302_153000_linux/)
    # 한글: 3-tier 테스트 — LOG_SESSION 주입으로 epoll+iouring 결과가
    #       동일 디렉토리(예: 20260302_153000_linux/)에 저장됨
    # -------------------------------------------------------------------------
    $env:LOG_SESSION = $LogSession
    $results = @{}

    function Run-ClientTest([string]$service) {
        Write-Host ""
        Write-Host "[run] docker-compose run --rm $service ..." -ForegroundColor Yellow
        docker-compose -f $ComposeFile run --rm $service
        return $LASTEXITCODE
    }

    $stepNum = 2
    if ($Backend -eq "both" -or $Backend -eq "epoll") {
        Write-Host ""
        Write-Host "[step $stepNum] Running epoll backend test..." -ForegroundColor Yellow
        $results["epoll"] = Run-ClientTest "client_epoll"
        $stepNum++
    }

    if ($Backend -eq "both" -or $Backend -eq "iouring") {
        Write-Host ""
        Write-Host "[step $stepNum] Running io_uring backend test..." -ForegroundColor Yellow
        $results["iouring"] = Run-ClientTest "client_iouring"
        $stepNum++
    }

    # English: Clear the env var to avoid leaking into subsequent commands
    # 한글: 이후 커맨드에 누출되지 않도록 환경 변수 제거
    Remove-Item Env:\LOG_SESSION -ErrorAction SilentlyContinue

    # -------------------------------------------------------------------------
    # English: Print test summary
    # 한글: 테스트 결과 요약 출력
    # -------------------------------------------------------------------------
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Test Results"                           -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    $allPassed = $true
    foreach ($kv in $results.GetEnumerator()) {
        $color  = if ($kv.Value -eq 0) { "Green" } else { "Red" }
        $status = if ($kv.Value -eq 0) { "PASS" }  else { "FAIL (exit $($kv.Value))" }
        Write-Host ("  {0,-10} : {1}" -f $kv.Key, $status) -ForegroundColor $color
        if ($kv.Value -ne 0) { $allPassed = $false }
    }

    $SessionLogPath = Join-Path $LogDir $LogSession
    if (Test-Path $SessionLogPath) {
        Write-Host ""
        Write-Host "  Log files: Doc\Performance\Logs\$LogSession\" -ForegroundColor DarkGray
        Get-ChildItem $SessionLogPath | ForEach-Object {
            Write-Host ("    " + $_.Name) -ForegroundColor DarkGray
        }
    }

    Write-Host "========================================" -ForegroundColor Cyan

    if ($allPassed) {
        Write-Host "  Overall: PASS" -ForegroundColor Green
    } else {
        Write-Host "  Overall: FAIL" -ForegroundColor Red
    }

    # -------------------------------------------------------------------------
    # English: -Push: commit new log files and push to origin/main
    # 한글: -Push: 새 로그 파일 커밋 후 origin/main 푸시
    # -------------------------------------------------------------------------
    if ($Push) {
        Write-Host ""
        Write-Host "[push] Staging new log files..." -ForegroundColor Yellow

        Push-Location $GitRoot
        try {
            # English: Stage only new files under Doc/Performance/Logs/ (no source changes)
            # 한글: Doc/Performance/Logs/ 하위 신규 파일만 스테이징 (소스 변경 없음)
            $RelativeLogDir = "NetworkModuleTest\Doc\Performance\Logs\$LogSession"
            git add $RelativeLogDir

            # English: Check if there is anything to commit
            # 한글: 커밋할 내용이 있는지 확인
            $staged = git diff --cached --name-only
            if (-not $staged) {
                Write-Host "[push] No new log files to commit." -ForegroundColor DarkGray
            } else {
                $passStr  = if ($allPassed) { "PASS" } else { "FAIL" }
                $backends = ($results.Keys | Sort-Object) -join "+"
                $msg = "perf: Linux Docker 테스트 결과 $LogSession [$backends] $passStr"

                git commit -m "$msg

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"

                git push origin main

                Write-Host "[push] Results pushed to origin/main." -ForegroundColor Green
                Write-Host "[push] Commit: $msg" -ForegroundColor DarkGray
            }
        } finally {
            Pop-Location
        }
    } else {
        Write-Host ""
        Write-Host "  Tip: re-run with -Push to commit and push results automatically." -ForegroundColor DarkGray
    }

    # English: Exit with failure if any backend failed
    # 한글: 하나라도 실패 시 비정상 종료
    if (-not $allPassed) { exit 1 }

} finally {
    Pop-Location
    Remove-Item Env:\LOG_SESSION -ErrorAction SilentlyContinue
}
