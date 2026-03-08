# run_oledb_test.ps1 — Build and run DB functional test against SQL Server via OLE DB
#
# Steps:
#   1. Set up MSVC x64 environment
#   2. Verify MSOLEDBSQL / SQLOLEDB provider is available (no installer needed — ships with Windows)
#   3. Start Docker Desktop if stopped
#   4. Pull + start SQL Server 2022 container if not running
#   5. Wait until SQL Server is ready
#   6. Create test database
#   7. Compile db_functional_test.cpp -> build\db_test_oledb.exe
#   8. Run test with OLE DB connection string
#   9. Stop + remove container (unless -KeepContainer)
#
# Usage:  .\run_oledb_test.ps1 [-Clean] [-KeepBuild] [-KeepContainer] [-SaPassword <pwd>] [-Provider <name>]
#
# -Provider:  "MSOLEDBSQL"  (Microsoft OLE DB Driver for SQL Server, recommended)
#             "SQLOLEDB"    (legacy Windows built-in, always available)
#             Default: auto-detect MSOLEDBSQL, fall back to SQLOLEDB

param(
    [switch]$Clean,
    [switch]$KeepBuild,
    [switch]$KeepContainer,
    [string]$SaPassword = 'Test1234!',
    [string]$Provider   = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

# ── Constants ──────────────────────────────────────────────────────────────────

$CONTAINER_NAME = "db_test_mssql"   # reuse the MSSQL container
$MSSQL_IMAGE    = "mcr.microsoft.com/mssql/server:2022-latest"
$MSSQL_PORT     = 1433
$DB_NAME        = "db_func_test"
$EXE_NAME       = "db_test_oledb.exe"

# ── Step 1: MSVC environment ───────────────────────────────────────────────────

Set-VcEnv

# ── Step 2: OLE DB provider check ─────────────────────────────────────────────

Write-Step "OLE DB provider check"

# Determine which provider to use
if ($Provider -eq '') {
    # Auto-detect: prefer MSOLEDBSQL (modern) over SQLOLEDB (legacy)
    $regPaths = @(
        'HKLM:\SOFTWARE\Classes\MSOLEDBSQL'
        'HKLM:\SOFTWARE\Classes\MSOLEDBSQL19'
    )
    $found = $regPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($found) {
        # Extract just the class name (last segment)
        $Provider = Split-Path $found -Leaf
        Write-Ok "Found modern provider: $Provider"
    } else {
        # SQLOLEDB is always present on Windows
        $Provider = 'SQLOLEDB'
        Write-Warn "MSOLEDBSQL not found — using legacy provider: SQLOLEDB"
        Write-Info "  (Install 'Microsoft OLE DB Driver for SQL Server' for better support)"
    }
} else {
    Write-Ok "Using specified provider: $Provider"
}

# English: OLE DB connection string — Provider=<name>;Data Source=...
# 한글: OLE DB 연결 문자열 — Provider=<이름>;Data Source=...
$CONN_STR = "Provider=$Provider;Data Source=localhost,$MSSQL_PORT;" +
            "Initial Catalog=$DB_NAME;User Id=sa;Password=$SaPassword;" +
            "TrustServerCertificate=yes;"

# ── Step 3: Docker ────────────────────────────────────────────────────────────

Assert-DockerRunning

# ── Step 4: SQL Server container ──────────────────────────────────────────────

Write-Step "SQL Server container"

$existing = docker ps -a --filter "name=^${CONTAINER_NAME}$" --format '{{.Status}}' 2>$null

if ($existing -match '^Up') {
    Write-Ok "Container '$CONTAINER_NAME' already running"
} elseif ($existing -match '^Exited') {
    Write-Info "Container '$CONTAINER_NAME' exists but stopped — starting..."
    docker start $CONTAINER_NAME | Out-Null
} else {
    Write-Info "Pulling $MSSQL_IMAGE ..."
    docker pull $MSSQL_IMAGE

    Write-Info "Creating container '$CONTAINER_NAME'..."
    docker run -d `
        --name $CONTAINER_NAME `
        -e "ACCEPT_EULA=Y" `
        -e "MSSQL_SA_PASSWORD=$SaPassword" `
        -p "${MSSQL_PORT}:1433" `
        $MSSQL_IMAGE | Out-Null
}

# ── Step 5: Wait for SQL Server ────────────────────────────────────────────────

Wait-ContainerReady -ContainerName $CONTAINER_NAME -Port $MSSQL_PORT -TimeoutSec 90
Write-Info "Waiting an extra 10s for SQL Server login readiness..."
Start-Sleep -Seconds 10

# ── Step 6: Ensure database exists ────────────────────────────────────────────

Write-Info "Ensuring database '$DB_NAME' exists..."
$createDbSql = "IF DB_ID('$DB_NAME') IS NULL CREATE DATABASE [$DB_NAME];"
docker exec $CONTAINER_NAME /opt/mssql-tools/bin/sqlcmd `
    -S "localhost,$MSSQL_PORT" -U sa -P $SaPassword -Q $createDbSql 2>$null
if ($LASTEXITCODE -ne 0) {
    docker exec $CONTAINER_NAME /opt/mssql-tools18/bin/sqlcmd `
        -S "localhost,$MSSQL_PORT" -U sa -P $SaPassword -C -Q $createDbSql
}
Write-Ok "Database '$DB_NAME' ready"

# ── Step 7: Compile ────────────────────────────────────────────────────────────

Write-Step "Compiling"

if ($Clean) { Remove-BuildDir }
Ensure-BuildDir

$cl       = Get-ClExe
$includes = Get-ServerIncludes
$srcRoot  = $script:ServerRoot
$buildDir = $script:BuildDir
$exePath  = Join-Path $buildDir $EXE_NAME

$sources = @(
    $script:SrcFile
    Join-Path $srcRoot "Database\OLEDBDatabase.cpp"
)

$clArgs = @(
    '/nologo', '/std:c++17', '/EHsc', '/W3', '/O1', '/utf-8'
    '/DUSE_OLEDB'
    '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'
) + $includes + $sources + @(
    "/Fe`"$exePath`""
    "/Fo`"$buildDir\\`""
    'ole32.lib', 'oleaut32.lib', 'msdasc.lib'
)

Write-Info "cl.exe $($clArgs -join ' ')"
& $cl @clArgs

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed (exit $LASTEXITCODE)"
}
Write-Ok "Built: $exePath"

# ── Step 8: Run ────────────────────────────────────────────────────────────────

Write-Step "Running DB functional test (OLE DB / $Provider)"
Write-Info "Connection: $CONN_STR"

& $exePath $CONN_STR
$exitCode = $LASTEXITCODE

# ── Step 9: Cleanup ────────────────────────────────────────────────────────────

if (-not $KeepContainer) {
    Write-Info "Stopping and removing container '$CONTAINER_NAME'..."
    docker stop $CONTAINER_NAME | Out-Null
    docker rm   $CONTAINER_NAME | Out-Null
    Write-Ok "Container removed."
}

if (-not $KeepBuild) { Remove-BuildDir }

Write-Step "Done"
if ($exitCode -eq 0) {
    Write-Ok "All tests passed."
} else {
    Write-Err "Test FAILED (exit $exitCode)"
}
exit $exitCode
