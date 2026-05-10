# run_postgres_test.ps1 — Build and run DB functional test against PostgreSQL via Docker
#
# Steps:
#   1. Set up MSVC x64 environment
#   2. Install psqlODBC driver if missing
#   3. Start Docker Desktop if stopped
#   4. Pull + start PostgreSQL container if not running
#   5. Wait until PostgreSQL is ready
#   6. Create test database
#   7. Compile db_functional_test.cpp → build\db_test_pgsql.exe
#   8. Run test with ODBC connection string
#   9. Stop + remove container (unless -KeepContainer)
#
# Usage:  .\run_postgres_test.ps1 [-Clean] [-KeepBuild] [-KeepContainer] [-PgPassword <pwd>]

param(
    [switch]$Clean,
    [switch]$KeepBuild,
    [switch]$KeepContainer,
    [string]$PgPassword = 'Test1234!'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

# ── Constants ──────────────────────────────────────────────────────────────────

$ODBC_DRIVER_NAME  = "PostgreSQL Unicode(x64)"   # driver string after psqlODBC install
# Direct MSI — latest release from https://github.com/postgresql-interfaces/psqlodbc/releases
$PSQLODBC_MSI_URL  = "https://github.com/postgresql-interfaces/psqlodbc/releases/download/REL-17_00_0007/psqlodbc_x64.msi"
$CONTAINER_NAME    = "db_test_pgsql"
$PG_IMAGE          = "postgres:16-alpine"
$PG_PORT           = 5432
$PG_USER           = "postgres"
$DB_NAME           = "db_func_test"
$EXE_NAME          = "db_test_pgsql.exe"

$CONN_STR = "Driver={$ODBC_DRIVER_NAME};Server=localhost;Port=$PG_PORT;" +
            "Database=$DB_NAME;UID=$PG_USER;PWD=$PgPassword;"

# ── Step 1: MSVC environment ───────────────────────────────────────────────────

Set-VcEnv

# ── Step 2: psqlODBC driver ────────────────────────────────────────────────────

Write-Step "psqlODBC driver check"

$drivers = Get-OdbcDriver -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
if ($drivers -contains $ODBC_DRIVER_NAME) {
    Write-Ok "Driver already installed: $ODBC_DRIVER_NAME"
} else {
    Write-Warn "$ODBC_DRIVER_NAME not found — installing psqlODBC..."
    $msiPath = Join-Path $env:TEMP "psqlodbc_x64.msi"

    Write-Info "Downloading psqlODBC MSI from postgresql.org..."
    Invoke-WebRequest -Uri $PSQLODBC_MSI_URL -OutFile $msiPath -UseBasicParsing

    Write-Info "Running installer (silent)..."
    $proc = Start-Process msiexec.exe -ArgumentList @('/i', $msiPath, '/quiet', '/norestart') -Wait -PassThru
    Remove-Item $msiPath -ErrorAction SilentlyContinue

    if ($proc.ExitCode -notin @(0, 3010)) {
        throw "psqlODBC install failed (msiexec exit $($proc.ExitCode))"
    }
    Write-Ok "psqlODBC installed"
}

# ── Step 3: Docker ────────────────────────────────────────────────────────────

Assert-DockerRunning

# ── Step 4: PostgreSQL container ──────────────────────────────────────────────

Write-Step "PostgreSQL container"

$existing = docker ps -a --filter "name=^${CONTAINER_NAME}$" --format '{{.Status}}' 2>$null

if ($existing -match '^Up') {
    Write-Ok "Container '$CONTAINER_NAME' already running"
} elseif ($existing -match '^Exited') {
    Write-Info "Container '$CONTAINER_NAME' exists but stopped — starting..."
    docker start $CONTAINER_NAME | Out-Null
} else {
    Write-Info "Pulling $PG_IMAGE ..."
    docker pull $PG_IMAGE

    Write-Info "Creating container '$CONTAINER_NAME'..."
    docker run -d `
        --name $CONTAINER_NAME `
        -e "POSTGRES_PASSWORD=$PgPassword" `
        -p "${PG_PORT}:5432" `
        --health-cmd "pg_isready -U $PG_USER" `
        --health-interval 3s `
        --health-timeout 5s `
        --health-retries 10 `
        $PG_IMAGE | Out-Null
}

# ── Step 5: Wait for PostgreSQL ───────────────────────────────────────────────

Wait-ContainerReady -ContainerName $CONTAINER_NAME -Port $PG_PORT -TimeoutSec 60
Write-Info "Waiting for PostgreSQL login readiness (up to 30s)..."
$ready = $false
$deadline = (Get-Date).AddSeconds(30)
do {
    Start-Sleep -Seconds 2
    docker exec $CONTAINER_NAME psql -U $PG_USER -d postgres -tAc "SELECT 1" 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $ready = $true
        break
    }
} while ((Get-Date) -lt $deadline)

if (-not $ready) {
    throw "PostgreSQL did not become query-ready within 30 seconds"
}
Write-Ok "PostgreSQL login ready"

# ── Step 6: Create test database ──────────────────────────────────────────────

Write-Info "Ensuring database '$DB_NAME' exists..."
$checkSql = "SELECT 1 FROM pg_database WHERE datname='$DB_NAME';"
$result   = docker exec $CONTAINER_NAME psql -U $PG_USER -tAc $checkSql 2>$null
if ($result -ne '1') {
    docker exec $CONTAINER_NAME createdb -U $PG_USER $DB_NAME
    Write-Ok "Database '$DB_NAME' created"
} else {
    Write-Ok "Database '$DB_NAME' already exists"
}

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
    Join-Path $srcRoot "Database\ODBCDatabase.cpp"
)

$clArgs = @(
    '/nologo', '/std:c++17', '/EHsc', '/W3', '/O1', '/utf-8'
    '/DUSE_PGSQL'
    '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'
) + $includes + $sources + @(
    "/Fe`"$exePath`""
    "/Fo`"$buildDir\\`""
    'odbc32.lib', 'odbccp32.lib'
)

Write-Info "cl.exe $($clArgs -join ' ')"
& $cl @clArgs

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed (exit $LASTEXITCODE)"
}
Write-Ok "Built: $exePath"

# ── Step 8: Run ────────────────────────────────────────────────────────────────

Write-Step "Running DB functional test (PostgreSQL)"
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
