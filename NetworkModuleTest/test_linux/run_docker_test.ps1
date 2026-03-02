# =============================================================================
# run_docker_test.ps1 — Windows host launcher for Docker Linux integration tests
# =============================================================================
# English: Builds Docker images and runs epoll + io_uring integration tests.
#          Run from anywhere; script locates NetworkModuleTest root automatically.
# 한글: Docker 이미지 빌드 후 epoll + io_uring 통합 테스트 실행.
#       어디서든 실행 가능; 스크립트가 NetworkModuleTest 루트를 자동으로 찾음.
#
# Usage:
#   .\test_linux\run_docker_test.ps1                    # build + run both backends
#   .\test_linux\run_docker_test.ps1 -Backend epoll     # epoll only
#   .\test_linux\run_docker_test.ps1 -Backend iouring   # io_uring only
#   .\test_linux\run_docker_test.ps1 -NoBuild           # skip image build
#   .\test_linux\run_docker_test.ps1 -Single            # run_integration.sh (no compose)

param(
    [ValidateSet("both", "epoll", "iouring")]
    [string]$Backend = "both",

    [switch]$NoBuild,

    # English: Run single-container integration test (no 3-tier compose)
    # 한글: 단일 컨테이너 통합 테스트 (3-tier compose 없이)
    [switch]$Single
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# English: Locate NetworkModuleTest root (parent of this script's directory)
# 한글: NetworkModuleTest 루트 찾기 (스크립트 디렉토리의 상위)
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot   = Split-Path -Parent $ScriptDir   # NetworkModuleTest/
$ComposeFile = Join-Path $ScriptDir "docker-compose.yml"

Push-Location $RepoRoot

try {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  NetworkModuleTest Docker Linux Test"   -ForegroundColor Cyan
    Write-Host "  Root   : $RepoRoot"                    -ForegroundColor Cyan
    Write-Host "  Backend: $Backend"                     -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""

    # -------------------------------------------------------------------------
    # English: Build Docker image
    # 한글: Docker 이미지 빌드
    # -------------------------------------------------------------------------
    if (-not $NoBuild) {
        Write-Host "[step 1/3] Building Docker image..." -ForegroundColor Yellow
        docker-compose -f $ComposeFile build
        if ($LASTEXITCODE -ne 0) { throw "docker-compose build failed (exit $LASTEXITCODE)" }
        Write-Host "[step 1/3] Image build complete." -ForegroundColor Green
    } else {
        Write-Host "[step 1/3] Skipping image build (-NoBuild)." -ForegroundColor DarkGray
    }

    # -------------------------------------------------------------------------
    # English: Single-container mode (no 3-tier compose)
    # 한글: 단일 컨테이너 모드
    # -------------------------------------------------------------------------
    if ($Single) {
        Write-Host ""
        Write-Host "[step 2/3] Single-container integration test..." -ForegroundColor Yellow
        docker-compose -f $ComposeFile run --rm build `
            /workspace/test_linux/scripts/run_integration.sh
        if ($LASTEXITCODE -ne 0) { throw "Integration test failed (exit $LASTEXITCODE)" }
        Write-Host "[step 2/3] Single-container test PASSED." -ForegroundColor Green
        return
    }

    # -------------------------------------------------------------------------
    # English: 3-tier compose tests
    # 한글: 3-tier compose 테스트
    # -------------------------------------------------------------------------
    $results = @{}

    function Run-ClientTest([string]$service) {
        Write-Host ""
        Write-Host "[run] docker-compose run --rm $service ..." -ForegroundColor Yellow
        docker-compose -f $ComposeFile run --rm $service
        return $LASTEXITCODE
    }

    if ($Backend -eq "both" -or $Backend -eq "epoll") {
        Write-Host ""
        Write-Host "[step 2/3] Running epoll backend test..." -ForegroundColor Yellow
        $results["epoll"] = Run-ClientTest "client_epoll"
    }

    if ($Backend -eq "both" -or $Backend -eq "iouring") {
        Write-Host ""
        Write-Host "[step 3/3] Running io_uring backend test..." -ForegroundColor Yellow
        $results["iouring"] = Run-ClientTest "client_iouring"
    }

    # -------------------------------------------------------------------------
    # English: Print summary
    # 한글: 결과 요약 출력
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

    Write-Host "========================================" -ForegroundColor Cyan

    if ($allPassed) {
        Write-Host "  Overall: PASS" -ForegroundColor Green
    } else {
        Write-Host "  Overall: FAIL" -ForegroundColor Red
        exit 1
    }

} finally {
    Pop-Location
}
