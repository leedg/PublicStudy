# run-db-tests.ps1 — DBModuleTest 수동 테스트 런처
#
# 사용법:
#   .\scripts\run-db-tests.ps1 [-Backend sqlite|mssql|pgsql|mysql|oledb|all]
#                               [-Config Debug|Release]
#                               [-ConnStr <연결 문자열>]
#                               [-Build] [-Rebuild]
#
# 환경변수로 연결 문자열을 미리 지정할 수 있습니다:
#   DB_MSSQL_ODBC  DB_PGSQL_ODBC  DB_MYSQL_ODBC  DB_OLEDB
#
# 예시:
#   .\scripts\run-db-tests.ps1                          # 대화형 메뉴
#   .\scripts\run-db-tests.ps1 -Backend sqlite          # SQLite만
#   .\scripts\run-db-tests.ps1 -Backend all -Build      # 전체 (빌드 포함)
#   $env:DB_MSSQL_ODBC="Driver=..."; .\scripts\run-db-tests.ps1 -Backend mssql

param(
    [ValidateSet('sqlite','mssql','pgsql','mysql','oledb','all','interactive')]
    [string]$Backend = 'interactive',

    [ValidateSet('Debug','Release')]
    [string]$Config = 'Debug',

    [string]$ConnStr = '',   # 단일 백엔드 지정 시 연결 문자열 오버라이드

    [switch]$Build,           # 실행 전 빌드
    [switch]$Rebuild          # 클린 빌드
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$vcxproj    = Join-Path $projectDir 'DBModuleTest.vcxproj'
$binDir     = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path "x64\$Config"
$exePath    = Join-Path $binDir "DBModuleTest.exe"

# ── MSBuild 탐색 ──────────────────────────────────────────────────────────

function Find-MSBuild {
    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($vswhere) {
        $vs = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vs) {
            $candidate = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
            if (Test-Path $candidate) { return $candidate }
            $candidate = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

# ── 빌드 ─────────────────────────────────────────────────────────────────

function Invoke-Build {
    param([switch]$Clean)

    $msbuild = Find-MSBuild
    if (-not $msbuild) { throw 'MSBuild.exe를 찾을 수 없습니다. Visual Studio를 설치하세요.' }
    Write-Host "[BUILD] MSBuild: $msbuild"

    $target = if ($Clean) { 'Rebuild' } else { 'Build' }
    & $msbuild $vcxproj /p:Configuration=$Config /p:Platform=x64 /t:$target /m /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "빌드 실패 (exit $LASTEXITCODE)" }
    Write-Host "[BUILD] 완료: $binDir"
}

# ── 단일 백엔드 비대화형 실행 ─────────────────────────────────────────────

function Run-Backend {
    param([int]$MenuNum, [string]$Label, [string]$ConnOverride)

    Write-Host "`n[$Label] 테스트 시작..."

    # 연결 문자열 오버라이드 (환경변수)
    $input = "$MenuNum`n"   # 백엔드 선택
    if ($MenuNum -eq 1) {
        # SQLite — 연결 문자열 입력 없음
        $input += "`n"
    } else {
        # 연결 문자열 입력 (빈 줄 = 기본값, 아니면 오버라이드)
        $input += "$ConnOverride`n"
    }
    $input += "`n"  # Enter를 눌러 계속
    $input += "0`n" # 종료

    $output = $input | & $exePath 2>&1
    Write-Host $output

    # 결과 파싱
    if ($output -match 'ALL OK') { return $true  }
    if ($output -match 'FAILED') { return $false }
    return $true  # 연결 실패는 경고로 처리
}

# ── 메인 ─────────────────────────────────────────────────────────────────

Write-Host ''
Write-Host '=== DBModuleTest — Network::Database 모듈 수동 테스트 ===' -ForegroundColor Magenta
Write-Host "프로젝트: $projectDir"
Write-Host "설정:     $Config / x64"
Write-Host ''

if ($Build -or $Rebuild) { Invoke-Build -Clean:$Rebuild }

if (-not (Test-Path $exePath)) {
    Write-Host "[WARN] 실행 파일이 없습니다: $exePath" -ForegroundColor Yellow
    Write-Host "       -Build 옵션으로 먼저 빌드하거나 Visual Studio에서 빌드하세요." -ForegroundColor Yellow
    $ans = Read-Host "지금 빌드할까요? (y/N)"
    if ($ans -match '^[yY]') { Invoke-Build }
    else { exit 1 }
}

# ── 대화형 모드 ───────────────────────────────────────────────────────────
if ($Backend -eq 'interactive') {
    Write-Host '대화형 모드로 실행합니다 (메뉴에서 백엔드를 선택하세요).'
    Write-Host ''
    & $exePath
    exit $LASTEXITCODE
}

# ── 비대화형 모드 ─────────────────────────────────────────────────────────

$map = @{
    'sqlite' = @{ Num = 1; Label = 'SQLite';    Env = '' }
    'mssql'  = @{ Num = 2; Label = 'MSSQL ODBC'; Env = 'DB_MSSQL_ODBC' }
    'pgsql'  = @{ Num = 3; Label = 'PostgreSQL ODBC'; Env = 'DB_PGSQL_ODBC' }
    'mysql'  = @{ Num = 4; Label = 'MySQL ODBC'; Env = 'DB_MYSQL_ODBC' }
    'oledb'  = @{ Num = 5; Label = 'OLE DB';    Env = 'DB_OLEDB' }
}

$targets = if ($Backend -eq 'all') { @('sqlite','mssql','pgsql','mysql','oledb') }
           else                    { @($Backend) }

$results = @()
foreach ($t in $targets) {
    $info = $map[$t]
    $conn = ''
    if ($ConnStr) { $conn = $ConnStr }
    elseif ($info.Env) {
        $envVal = [System.Environment]::GetEnvironmentVariable($info.Env)
        if ($envVal) { $conn = $envVal }
    }

    $ok = Run-Backend -MenuNum $info.Num -Label $info.Label -ConnOverride $conn
    $results += [PSCustomObject]@{ Backend = $info.Label; OK = $ok }
}

Write-Host ''
Write-Host '=== 최종 요약 ===' -ForegroundColor Magenta
$allOk = $true
foreach ($r in $results) {
    $mark  = if ($r.OK) { 'PASS' } else { 'FAIL'; $allOk = $false }
    $color = if ($r.OK) { 'Green' } else { 'Red' }
    Write-Host ("  [{0}] {1}" -f $mark, $r.Backend) -ForegroundColor $color
}

exit $(if ($allOk) { 0 } else { 1 })
