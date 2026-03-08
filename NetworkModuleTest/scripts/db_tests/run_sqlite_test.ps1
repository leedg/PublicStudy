# run_sqlite_test.ps1 — Build and run DB functional test against SQLite in-memory
#
# Steps:
#   1. Set up MSVC x64 environment
#   2. Download sqlite3 amalgamation if not cached
#   3. Compile db_functional_test.cpp + sqlite3.c → build\db_test_sqlite.exe
#   4. Run the test
#   5. (No teardown needed — in-memory DB, no Docker)
#
# Usage:  .\run_sqlite_test.ps1 [-Clean] [-KeepBuild]

param(
    [switch]$Clean,      # Wipe build dir before compiling
    [switch]$KeepBuild   # Don't delete build dir after test
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

# ── Constants ──────────────────────────────────────────────────────────────────

$SQLITE_VERSION   = "3450200"   # 3.45.2 — update as needed
$SQLITE_YEAR      = "2024"
$SQLITE_ZIP_URL   = "https://www.sqlite.org/$SQLITE_YEAR/sqlite-amalgamation-$SQLITE_VERSION.zip"
$SQLITE_CACHE_DIR = Join-Path $PSScriptRoot "cache\sqlite-amalgamation-$SQLITE_VERSION"
$SQLITE_C         = Join-Path $SQLITE_CACHE_DIR "sqlite3.c"
$SQLITE_H         = Join-Path $SQLITE_CACHE_DIR "sqlite3.h"
$EXE_NAME         = "db_test_sqlite.exe"

# ── Step 1: MSVC environment ───────────────────────────────────────────────────

Set-VcEnv

# ── Step 2: SQLite amalgamation ────────────────────────────────────────────────

Write-Step "SQLite amalgamation"

if ((Test-Path $SQLITE_C) -and (Test-Path $SQLITE_H)) {
    Write-Ok "SQLite $SQLITE_VERSION already cached at: $SQLITE_CACHE_DIR"
} else {
    Write-Info "Downloading SQLite $SQLITE_VERSION amalgamation..."
    $zipPath = Join-Path $env:TEMP "sqlite-amalgamation-$SQLITE_VERSION.zip"

    try {
        Invoke-WebRequest -Uri $SQLITE_ZIP_URL -OutFile $zipPath -UseBasicParsing
    } catch {
        throw "Failed to download SQLite from $SQLITE_ZIP_URL`n$_"
    }

    $extractTo = Join-Path $PSScriptRoot "cache"
    if (-not (Test-Path $extractTo)) { New-Item -ItemType Directory $extractTo | Out-Null }
    Expand-Archive -Path $zipPath -DestinationPath $extractTo -Force
    Remove-Item $zipPath -ErrorAction SilentlyContinue

    if (-not (Test-Path $SQLITE_C)) {
        throw "sqlite3.c not found after extraction — check archive structure"
    }
    Write-Ok "SQLite $SQLITE_VERSION extracted to: $SQLITE_CACHE_DIR"
}

# ── Step 3: Compile ────────────────────────────────────────────────────────────

Write-Step "Compiling"

if ($Clean) { Remove-BuildDir }
Ensure-BuildDir

$cl        = Get-ClExe
$includes  = Get-ServerIncludes
$srcRoot   = $script:ServerRoot
$buildDir  = $script:BuildDir
$exePath   = Join-Path $buildDir $EXE_NAME

# Source files needed (ServerEngine database headers are header-only; we compile the .cpp impls)
$sources = @(
    $script:SrcFile
    Join-Path $srcRoot "Database\SQLiteDatabase.cpp"
    $SQLITE_C
)

$clArgs = @(
    '/nologo', '/std:c++17', '/EHsc', '/W3', '/O1'
    '/DUSE_SQLITE', '/DHAVE_SQLITE3'
    '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'
    # SQLite compile flags (disable optional features to speed up build)
    '/DSQLITE_THREADSAFE=0', '/DSQLITE_OMIT_LOAD_EXTENSION'
) + $includes + @(
    "/I`"$SQLITE_CACHE_DIR`""
) + $sources + @(
    "/Fe`"$exePath`""
    "/Fo`"$buildDir\\`""
)

Write-Info "cl.exe $($clArgs -join ' ')"
& $cl @clArgs

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed (exit $LASTEXITCODE)"
}
Write-Ok "Built: $exePath"

# ── Step 4: Run ────────────────────────────────────────────────────────────────

Write-Step "Running DB functional test (SQLite in-memory)"

& $exePath
$exitCode = $LASTEXITCODE

# ── Step 5: Cleanup ────────────────────────────────────────────────────────────

if (-not $KeepBuild) { Remove-BuildDir }

Write-Step "Done"
if ($exitCode -eq 0) {
    Write-Ok "All tests passed."
} else {
    Write-Err "Test FAILED (exit $exitCode)"
}
exit $exitCode
